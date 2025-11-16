#pragma once

#include <Arduino.h>

namespace showduino {

// Packet layout:
// [0xAA][LEN][CMD][DATA ...][CHECKSUM]
constexpr uint8_t DUOFRAME_HEADER = 0xAA;
constexpr size_t DUOFRAME_MAX_PAYLOAD = 96;

enum DuoFrameCommand : uint8_t {
  DF_CMD_NONE = 0x00,
  DF_CMD_HEARTBEAT = 0x01,
  DF_CMD_STATUS = 0x02,
  DF_CMD_RELAY_SET = 0x10,
  DF_CMD_CONTROL_MODE = 0x11,
  DF_CMD_TIMELINE_SEEK = 0x12,
  DF_CMD_LED_PIXEL = 0x13,
  DF_CMD_AUDIO = 0x14,
  DF_CMD_BUTTON_EVENT = 0x40,
  DF_CMD_ADDON_LIST = 0x50,
  DF_CMD_EMERGENCY = 0xEE
};

struct DuoFrame {
  uint8_t command = DF_CMD_NONE;
  uint8_t length = 0;
  uint8_t payload[DUOFRAME_MAX_PAYLOAD]{};
};

inline uint8_t duoFrameChecksum(uint8_t len, uint8_t cmd, const uint8_t *data) {
  uint16_t sum = len + cmd;
  for (uint8_t i = 0; i < len; ++i) {
    sum += data[i];
  }
  return static_cast<uint8_t>(sum & 0xFF);
}

inline size_t duoFrameSerialize(const DuoFrame &frame, uint8_t *buffer, size_t bufferSize) {
  if (!buffer || bufferSize < (size_t)(frame.length + 4)) {
    return 0;
  }

  buffer[0] = DUOFRAME_HEADER;
  buffer[1] = frame.length;
  buffer[2] = frame.command;

  for (uint8_t i = 0; i < frame.length; ++i) {
    buffer[3 + i] = frame.payload[i];
  }

  buffer[3 + frame.length] = duoFrameChecksum(frame.length, frame.command, frame.payload);
  return frame.length + 4;
}

inline bool duoFrameParse(const uint8_t *buffer, size_t length, DuoFrame &out) {
  if (!buffer || length < 4 || buffer[0] != DUOFRAME_HEADER) {
    return false;
  }

  uint8_t payloadLen = buffer[1];
  if ((size_t)(payloadLen + 4) != length || payloadLen > DUOFRAME_MAX_PAYLOAD) {
    return false;
  }

  uint8_t command = buffer[2];
  uint8_t checksum = buffer[3 + payloadLen];
  uint8_t calc = duoFrameChecksum(payloadLen, command, &buffer[3]);

  if (checksum != calc) {
    return false;
  }

  out.command = command;
  out.length = payloadLen;
  memcpy(out.payload, &buffer[3], payloadLen);
  return true;
}

}  // namespace showduino
