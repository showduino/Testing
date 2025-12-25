#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace showduino {

enum class MsgType : uint8_t {
  COMMAND = 1,
  STATUS = 2,
  ACK = 3,
  HEARTBEAT = 4,
  INVENTORY = 5,
  FILE_OP = 6,
};

enum class ErrCode : uint8_t {
  OK = 0,
  BAD_REQ,
  DENIED,
  NOT_FOUND,
  BUSY,
  TIMEOUT,
  IO_FAIL,
  CRC_FAIL,
  UNSUPPORTED,
  SAFETY_LOCK,
  DEGRADED,
};

// Binary header + JSON payload body.
// Framing: COBS encode(header+payload+crc) with optional 0x00 delimiter on UART.
struct __attribute__((packed)) PacketHeader {
  uint16_t magic;      // 'S''D' = 0x5344
  uint8_t version;     // 1
  uint8_t type;        // MsgType
  uint16_t src;        // numeric node id
  uint16_t dst;        // numeric node id (or 0xFFFF broadcast)
  uint32_t id;         // message id (dedupe)
  uint32_t seq;        // per-src increment
  uint32_t ts_ms;      // sender millis()
  uint16_t payload_len;// bytes following header (JSON UTF-8)
  uint16_t crc16;      // CRC16 over header (crc16=0) + payload
};

constexpr uint16_t kMagic = 0x5344;
constexpr uint16_t kBroadcast = 0xFFFF;

struct PacketView {
  PacketHeader hdr{};
  const uint8_t *payload = nullptr;
};

// Encode packet into COBS buffer (no UART delimiter appended).
// Returns encoded length (0 on failure).
size_t proto_encode_cobs(const PacketHeader &hdr_in,
                         const uint8_t *payload,
                         size_t payload_len,
                         uint8_t *out,
                         size_t out_max);

// Decode a COBS buffer into a raw packet (header+payload).
// Returns decoded length (0 on failure).
size_t proto_decode_cobs(const uint8_t *cobs_in, size_t cobs_len, uint8_t *out_raw, size_t out_max);

// Validate decoded raw packet and populate PacketView.
bool proto_parse_raw(const uint8_t *raw, size_t raw_len, PacketView &out);

// Small helper for logging type names
const char *msg_type_name(MsgType t);

}  // namespace showduino

