#include "dmx.h"

namespace {

uint8_t dmxUniverse[512]{};

}  // namespace

void dmx_begin() {
  // Initialize DMX UART / transceiver here
}

void dmx_handleCommand(const showduino::DuoFrame &frame) {
  if (frame.length < 2) return;
  uint8_t channel = frame.payload[0];
  uint8_t value = frame.payload[1];
  if (channel < sizeof(dmxUniverse)) {
    dmxUniverse[channel] = value;
  }
}

void dmx_blackout() {
  memset(dmxUniverse, 0, sizeof(dmxUniverse));
}
