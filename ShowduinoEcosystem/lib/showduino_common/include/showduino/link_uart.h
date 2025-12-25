#pragma once

#include <Arduino.h>

#include <showduino/proto.h>
#include <showduino/uart_framer.h>

namespace showduino {

using UartPacketFn = void (*)(const PacketView &pkt, void *user);

class UartLink {
 public:
  UartLink(HardwareSerial &ser, int rx_pin, int tx_pin, uint32_t baud);

  void begin();
  void poll(UartPacketFn cb, void *user);

  // Sends packet (COBS framed with 0x00 delimiter).
  bool sendPacket(const PacketHeader &hdr, const uint8_t *payload, size_t payload_len);

 private:
  HardwareSerial &ser_;
  int rx_;
  int tx_;
  uint32_t baud_;
  UartCobsFramer framer_{};

  uint8_t raw_[1200]{};
};

}  // namespace showduino

