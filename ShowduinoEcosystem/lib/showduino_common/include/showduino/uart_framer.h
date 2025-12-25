#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace showduino {

// Collects COBS frames delimited by 0x00 on UART streams.
class UartCobsFramer {
 public:
  explicit UartCobsFramer(size_t max_frame = 1024) : max_(max_frame) {}

  // Feed one byte. Returns true when a complete frame is available.
  bool feed(uint8_t b) {
    if (b == 0x00) {
      if (len_ == 0) return false;  // ignore empty
      frame_ready_ = true;
      return true;
    }
    if (len_ >= sizeof(buf_)) {
      // overflow -> reset
      reset();
      return false;
    }
    buf_[len_++] = b;
    return false;
  }

  const uint8_t *frame_data() const { return buf_; }
  size_t frame_len() const { return len_; }
  bool frame_ready() const { return frame_ready_; }

  void consume() { reset(); }

 private:
  void reset() {
    len_ = 0;
    frame_ready_ = false;
  }

  size_t max_ = 1024;
  uint8_t buf_[1200]{};
  size_t len_ = 0;
  bool frame_ready_ = false;
};

}  // namespace showduino

