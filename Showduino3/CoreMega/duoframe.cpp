#include "duoframe.h"

#include "config.h"

using namespace showduino;

namespace {

HardwareSerial *serialRef = nullptr;
duoframe::FrameCallback callbackRef = nullptr;

enum ParserState { WAIT_HEADER, WAIT_LENGTH, WAIT_BODY };
ParserState parserState = WAIT_HEADER;
uint8_t buffer[DUOFRAME_MAX_PAYLOAD + 4];
uint8_t expected = 0;
uint8_t indexPos = 0;

void resetParser() {
  parserState = WAIT_HEADER;
  expected = 0;
  indexPos = 0;
}

}  // namespace

namespace duoframe {

void begin(HardwareSerial &serialPort, uint32_t baud, uint8_t rxPin, uint8_t txPin, FrameCallback cb) {
  serialRef = &serialPort;
  callbackRef = cb;
  serialRef->begin(baud, SERIAL_8N1, rxPin, txPin);
  resetParser();
}

bool send(showduino::DuoFrameCommand cmd, const uint8_t *payload, uint8_t length) {
  DuoFrame frame;
  frame.command = cmd;
  frame.length = length;
  if (payload && length) {
    memcpy(frame.payload, payload, length);
  }
  return sendFrame(frame);
}

bool sendFrame(const DuoFrame &frame) {
  if (!serialRef) return false;
  uint8_t packet[DUOFRAME_MAX_PAYLOAD + 4];
  size_t len = duoFrameSerialize(frame, packet, sizeof(packet));
  if (!len) return false;
  return serialRef->write(packet, len) == len;
}

void poll() {
  if (!serialRef || !callbackRef) return;

  while (serialRef->available()) {
    uint8_t byteIn = serialRef->read();
    switch (parserState) {
      case WAIT_HEADER:
        if (byteIn == DUOFRAME_HEADER) {
          buffer[0] = byteIn;
          parserState = WAIT_LENGTH;
        }
        break;

      case WAIT_LENGTH:
        buffer[1] = byteIn;
        expected = byteIn + 2;  // cmd + payload + checksum
        indexPos = 0;
        parserState = WAIT_BODY;
        break;

      case WAIT_BODY:
        buffer[2 + indexPos] = byteIn;
        indexPos++;
        if (indexPos >= expected) {
          size_t frameLen = buffer[1] + 4;
          DuoFrame frame;
          if (duoFrameParse(buffer, frameLen, frame)) {
            callbackRef(frame);
          }
          resetParser();
        }
        break;
    }
  }
}

}  // namespace duoframe
