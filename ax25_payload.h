#ifndef APRSMSG_H
#define APRSMSG_H

#include <Arduino.h>

namespace AX25 {
  
class Payload
{
public:
  Payload(byte *rxPayload, int payloadLength);
  Payload(String inputText);
  
  String ToText(const String &customComment);
  bool ToBinary(byte *txPayload, int bufferLength);

private:
  String decodeCall(byte *rxPtr);
  bool encodeCall(String callsign, byte *txPtr, int bufferLength);

  bool parseString(String inputText);
  bool parsePayload(byte *rxPayload, int payloadLength);
  
private:
  enum AX25Ctrl {
    UI = 0x03
  };

  enum AX25Pid {
    NoLayer3 = 0xf0
  };

  const int CallsignSize = 7;
  const int RptMaxCount = 7;

private:
  String srcCall_, dstCall_;
  String rptCalls_[7];
  int rptCallsCount_;
  String info_;
};

} // AX25

#endif // APRSMSG_H
