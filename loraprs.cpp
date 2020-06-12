#include "loraprs.h"

LoraPrs::LoraPrs() 
  : serialBt_()
  , kissState_(KissState::Void)
  , kissCmd_(KissCmd::NoCmd)
{ 
}

void LoraPrs::setup(const LoraPrsConfig &conf)
{
  isClient_ = conf.IsClientMode;
  loraFreq_ = conf.LoraFreq;
  
  aprsLogin_ = String("user ") + conf.AprsLogin = String(" pass ") + 
    conf.AprsPass = String(" vers ") + CfgLoraprsVersion + String("\n");
  aprsHost_ = conf.AprsHost;
  aprsPort_ = conf.AprsPort;
  
  autoCorrectFreq_ = conf.EnableAutoFreqCorrection;
  addSignalReport_ = conf.EnableSignalReport;
  persistentConn_ = conf.EnablePersistentAprsConnection;
  enableIsToRf_ = conf.EnableIsToRf;
  enableRepeater_ = conf.EnableRepeater;
  
  setupWifi(conf.WifiSsid, conf.WifiKey);
  setupLora(conf.LoraFreq, conf.LoraBw, conf.LoraSf, conf.LoraCodingRate, conf.LoraPower, conf.LoraSync);
  setupBt(conf.BtName);
}

void LoraPrs::setupWifi(const String &wifiName, const String &wifiKey) 
{
  if (!isClient_) {
    Serial.print("WIFI connecting to " + wifiName);

    WiFi.setHostname("loraprs");
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiName.c_str(), wifiKey.c_str());

    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("ok");
    Serial.println(WiFi.localIP());
  }
}

void LoraPrs::reconnectWifi() {

  Serial.print("WIFI re-connecting...");

  while (WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0,0,0,0)) {
    WiFi.reconnect();
    delay(500);
    Serial.print(".");
  }

  Serial.println("ok");
}

bool LoraPrs::reconnectAprsis() {

  Serial.print("APRSIS re-connecting...");
  
  if (!wifiClient_.connect(aprsHost_.c_str(), aprsPort_)) {
    Serial.println("Failed to connect to " + aprsHost_ + ":" + aprsPort_);
    return false;
  }
  wifiClient_.print(aprsLogin_);
  return true;
}

void LoraPrs::setupLora(int loraFreq, int bw, byte sf, byte cr, byte pwr, byte sync)
{
  Serial.print("LoRa init...");
  
  LoRa.setPins(CfgPinSs, CfgPinRst, CfgPinDio0);
  
  while (!LoRa.begin(loraFreq)) {
    Serial.print(".");
    delay(500);
  }
  LoRa.setSyncWord(sync);
  LoRa.setSpreadingFactor(sf);
  LoRa.setSignalBandwidth(bw);
  LoRa.setCodingRate4(cr);
  LoRa.setTxPower(pwr);
  LoRa.enableCrc();
  
  Serial.println("ok");  
}

void LoraPrs::setupBt(const String &btName)
{
  if (isClient_) {
    Serial.print("BT init " + btName + "...");
  
    if (serialBt_.begin(btName)) {
      Serial.println("ok");
    }
    else
    {
      Serial.println("failed");
    }
  }
}

void LoraPrs::loop()
{
  if (WiFi.status() != WL_CONNECTED && !isClient_) {
    reconnectWifi();
  }
  if (serialBt_.available()) {
    onBtDataAvailable();
  }
  if (int packetSize = LoRa.parsePacket()) {
    onLoraDataAvailable(packetSize);
  }
  delay(10);
}

void LoraPrs::onRfAprsReceived(const String &aprsMessage)
{
  if (WiFi.status() != WL_CONNECTED) {
    // skip in client mode
    return;
  }
  
  if (!wifiClient_.connected()) {
    reconnectAprsis();
  }
  Serial.print(aprsMessage);
  wifiClient_.print(aprsMessage);

  if (!persistentConn_) {
    wifiClient_.stop();
  }
}

void LoraPrs::onLoraDataAvailable(int packetSize)
{
  int rxBufIndex = 0;
  byte rxBuf[packetSize];

  serialBt_.write(KissMarker::Fend);
  serialBt_.write(KissCmd::Data);

  while (LoRa.available()) {
    byte rxByte = LoRa.read();

    if (rxByte == KissMarker::Fend) {
      serialBt_.write(KissMarker::Fesc);
      serialBt_.write(KissMarker::Tfend);
    }
    else if (rxByte == KissMarker::Fesc) {
      serialBt_.write(KissMarker::Fesc);
      serialBt_.write(KissMarker::Tfesc);
    }
    else {
      rxBuf[rxBufIndex++] = rxByte;
      serialBt_.write(rxByte);
    }
  }

  serialBt_.write(KissMarker::Fend);

  float snr = LoRa.packetSnr();
  float rssi = LoRa.packetRssi();
  long frequencyError = LoRa.packetFrequencyError();

  String signalReport = String(" ") +
    String("rssi: ") +
    String(snr < 0 ? rssi + snr : rssi) +
    String("dBm, ") +
    String("snr: ") +
    String(snr) +
    String("dB, ") +
    String("err: ") +
    String(frequencyError) +
    String("Hz");

  if (autoCorrectFreq_) {
    loraFreq_ -= frequencyError;
    LoRa.setFrequency(loraFreq_);
  }

  String aprsMsg = AX25::Payload(rxBuf, rxBufIndex).ToText(addSignalReport_ ? signalReport : String());

  if (aprsMsg.length() != 0) {
    onRfAprsReceived(aprsMsg);
  }

  delay(50);
}

void LoraPrs::kissResetState()
{
  kissCmd_ = KissCmd::NoCmd;
  kissState_ = KissState::Void;
}

void LoraPrs::onBtDataAvailable() 
{ 
  while (serialBt_.available()) {
    byte txByte = serialBt_.read();

    switch (kissState_) {
      case KissState::Void:
        if (txByte == KissMarker::Fend) {
          kissCmd_ = KissCmd::NoCmd;
          kissState_ = KissState::GetCmd;
        }
        break;
      case KissState::GetCmd:
        if (txByte != KissMarker::Fend) {
          if (txByte == KissCmd::Data) {
            LoRa.beginPacket();
            kissCmd_ = (KissCmd)txByte;
            kissState_ = KissState::GetData;
          }
          else {
            kissResetState();
          }
        }
        break;
      case KissState::GetData:
        if (txByte == KissMarker::Fesc) {
          kissState_ = KissState::Escape;
        }
        else if (txByte == KissMarker::Fend) {
          if (kissCmd_ == KissCmd::Data) {
            LoRa.endPacket();
          }
          kissResetState();
        }
        else {
          LoRa.write(txByte);
        }
        break;
      case KissState::Escape:
        if (txByte == KissMarker::Tfend) {
          LoRa.write(KissMarker::Fend);
          kissState_ = KissState::GetData;
        }
        else if (txByte == KissMarker::Tfesc) {
          LoRa.write(KissMarker::Fesc);
          kissState_ = KissState::GetData;
        }
        else {
          kissResetState();
        }
        break;
      default:
        break;
    }
  }
  delay(20);
}
