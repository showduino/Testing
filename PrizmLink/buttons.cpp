#include "buttons.h"
#include "debug_utils.h"

namespace Buttons {

static Prizm::ButtonConfig sCfg;
static bool sReady = false;
static bool sEmergency = false;
static bool sStopLast = false;
static bool sCycleLast = false;
static bool sConfirmLast = false;

static bool readPin(uint8_t pin) {
  int val = digitalRead(pin);
  return sCfg.activeLow ? val == LOW : val == HIGH;
}

bool begin(const Prizm::ButtonConfig &cfg) {
  sCfg = cfg;
  uint8_t mode = cfg.activeLow ? INPUT_PULLUP : INPUT_PULLDOWN;
  pinMode(cfg.stopPin, mode);
  pinMode(cfg.cyclePin, mode);
  pinMode(cfg.confirmPin, mode);
  sReady = true;
  return true;
}

Event poll() {
  if (!sReady) return Event::None;

  bool stopPressed = readPin(sCfg.stopPin);
  bool cyclePressed = readPin(sCfg.cyclePin);
  bool confirmPressed = readPin(sCfg.confirmPin);

  Event event = Event::None;
  if (stopPressed && !sStopLast) {
    sEmergency = true;
    event = Event::EmergencyStop;
    Debug::warn("BTN", "Emergency stop engaged");
  } else if (cyclePressed && !sCycleLast) {
    event = Event::CycleMode;
  } else if (confirmPressed && !sConfirmLast) {
    event = Event::Confirm;
  }

  sStopLast = stopPressed;
  sCycleLast = cyclePressed;
  sConfirmLast = confirmPressed;
  return event;
}

bool emergencyLatched() {
  return sEmergency;
}

void clearEmergency() {
  sEmergency = false;
}

} // namespace Buttons

