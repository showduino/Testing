#include <showduino/link_uart.h>

#include <showduino/config.h>

namespace showduino {

UartLink::UartLink(HardwareSerial &ser, int rx_pin, int tx_pin, uint32_t baud)
    : ser_(ser), rx_(rx_pin), tx_(tx_pin), baud_(baud) {}

void UartLink::begin() {
  ser_.begin(baud_, SERIAL_8N1, rx_, tx_);
}

bool UartLink::sendPacket(const PacketHeader &hdr, const uint8_t *payload, size_t payload_len) {
  uint8_t cobs[1400];
  size_t enc = proto_encode_cobs(hdr, payload, payload_len, cobs, sizeof(cobs));
  if (!enc) return false;
  size_t w1 = ser_.write(cobs, enc);
  size_t w2 = ser_.write((uint8_t)0x00);
  return (w1 == enc) && (w2 == 1);
}

void UartLink::poll(UartPacketFn cb, void *user) {
  while (ser_.available()) {
    uint8_t b = static_cast<uint8_t>(ser_.read());
    if (!framer_.feed(b)) continue;
    if (!framer_.frame_ready()) continue;

    const uint8_t *cobs = framer_.frame_data();
    const size_t cobs_len = framer_.frame_len();
    size_t raw_len = proto_decode_cobs(cobs, cobs_len, raw_, sizeof(raw_));

    PacketView view{};
    if (raw_len && proto_parse_raw(raw_, raw_len, view)) {
      if (cb) cb(view, user);
    }

    framer_.consume();
  }
}

}  // namespace showduino

