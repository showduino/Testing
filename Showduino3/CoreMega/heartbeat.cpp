#include "heartbeat.h"

#include <Arduino.h>

#include "config.h"
#include "duoframe.h"

namespace {

uint32_t lastBeatSent = 0;
uint32_t lastBrainBeat = 0;
bool brainOnline = false;

}  // namespace

void heartbeat_begin() {
  lastBeatSent = 0;
  lastBrainBeat = 0;
  brainOnline = false;
}

void heartbeat_update() {
  uint32_t now = millis();
  if (now - lastBeatSent >= HEARTBEAT_INTERVAL_MS) {
    duoframe::send(showduino::DF_CMD_HEARTBEAT, nullptr, 0);
    lastBeatSent = now;
  }

  if (brainOnline && (now - lastBrainBeat) > (HEARTBEAT_INTERVAL_MS * 5)) {
    brainOnline = false;
  }
}

void heartbeat_handleFrame(const showduino::DuoFrame &frame) {
  if (frame.command == showduino::DF_CMD_HEARTBEAT) {
    brainOnline = true;
    lastBrainBeat = millis();
  }
}
