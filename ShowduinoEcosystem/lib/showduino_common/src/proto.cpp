#include <showduino/proto.h>

#include <string.h>

#include <showduino/cobs.h>
#include <showduino/crc16.h>

namespace showduino {

// Proper streaming CRC combine for CCITT (simple implementation: compute over concatenated buffers)
static uint16_t crc_over(const PacketHeader &hdr_no_crc, const uint8_t *payload, size_t payload_len) {
  PacketHeader tmp = hdr_no_crc;
  tmp.crc16 = 0;
  // Compute in one pass by copying into a small staging buffer when payload fits; else two-pass.
  uint16_t crc = crc16_ccitt_false(reinterpret_cast<const uint8_t *>(&tmp), sizeof(PacketHeader));
  if (!payload || payload_len == 0) return crc;
  // Continue CRC correctly by re-implementing incremental step:
  for (size_t i = 0; i < payload_len; ++i) {
    crc ^= static_cast<uint16_t>(payload[i]) << 8;
    for (uint8_t b = 0; b < 8; ++b) {
      if (crc & 0x8000) crc = static_cast<uint16_t>((crc << 1) ^ 0x1021);
      else crc = static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

size_t proto_encode_cobs(const PacketHeader &hdr_in,
                         const uint8_t *payload,
                         size_t payload_len,
                         uint8_t *out,
                         size_t out_max) {
  if (!out || out_max < 8) return 0;
  if (payload_len > 0xFFFF) return 0;

  PacketHeader hdr = hdr_in;
  hdr.magic = kMagic;
  hdr.payload_len = static_cast<uint16_t>(payload_len);
  hdr.crc16 = 0;
  hdr.crc16 = crc_over(hdr, payload, payload_len);

  const size_t raw_len = sizeof(PacketHeader) + payload_len;
  // Worst-case COBS expansion: + (raw_len/254) + 1; be conservative.
  if (out_max < raw_len + (raw_len / 254) + 2) return 0;

  // Build raw into a small stack buffer if feasible; otherwise encode in two steps is harder.
  // We keep raw packets small (UART up to ~1k, ESPNOW << 250), so stack is fine here.
  uint8_t raw[1200];
  if (raw_len > sizeof(raw)) return 0;

  memcpy(raw, &hdr, sizeof(PacketHeader));
  if (payload_len && payload) {
    memcpy(raw + sizeof(PacketHeader), payload, payload_len);
  }

  return cobs_encode(raw, raw_len, out, out_max);
}

size_t proto_decode_cobs(const uint8_t *cobs_in, size_t cobs_len, uint8_t *out_raw, size_t out_max) {
  if (!cobs_in || !out_raw) return 0;
  return cobs_decode(cobs_in, cobs_len, out_raw, out_max);
}

bool proto_parse_raw(const uint8_t *raw, size_t raw_len, PacketView &out) {
  if (!raw || raw_len < sizeof(PacketHeader)) return false;

  PacketHeader hdr{};
  memcpy(&hdr, raw, sizeof(PacketHeader));
  if (hdr.magic != kMagic) return false;
  if (hdr.version != 1) return false;
  const size_t expected = sizeof(PacketHeader) + hdr.payload_len;
  if (expected != raw_len) return false;

  const uint8_t *payload = raw + sizeof(PacketHeader);
  const uint16_t received_crc = hdr.crc16;
  hdr.crc16 = 0;
  const uint16_t computed = crc_over(hdr, payload, hdr.payload_len);
  if (computed != received_crc) return false;

  out.hdr = hdr;
  out.hdr.crc16 = received_crc;
  out.payload = payload;
  return true;
}

const char *msg_type_name(MsgType t) {
  switch (t) {
    case MsgType::COMMAND: return "COMMAND";
    case MsgType::STATUS: return "STATUS";
    case MsgType::ACK: return "ACK";
    case MsgType::HEARTBEAT: return "HEARTBEAT";
    case MsgType::INVENTORY: return "INVENTORY";
    case MsgType::FILE_OP: return "FILE_OP";
    default: return "?";
  }
}

}  // namespace showduino

