#include "emergency.h"

#include <Arduino.h>

#include "config.h"
#include "dmx.h"
#include "duoframe.h"

namespace {

bool lastState = false;

void broadcastEmergency(bool active) {
  uint8_t payload[1] = {static_cast<uint8_t>(active)};
  duoframe::send(showduino::DF_CMD_EMERGENCY, payload, sizeof(payload));
}

}  // namespace

void emergency_begin() {
  pinMode(EMERGENCY_PIN, EMERGENCY_ACTIVE_LOW ? INPUT_PULLUP : INPUT);
  lastState = false;
}

void emergency_update() {
  bool raw = digitalRead(EMERGENCY_PIN);
  bool active = EMERGENCY_ACTIVE_LOW ? (raw == LOW) : (raw == HIGH);
  if (active != lastState) {
    lastState = active;
    if (active) {
      dmx_blackout();
    }
    broadcastEmergency(active);
  }
}
