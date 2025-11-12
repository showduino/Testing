#include "pot_control.h"
#include <algorithm>

namespace PotControl {

static Prizm::PotConfig sCfg;
static bool sReady = false;
static PotReadings sSmoothed;

static float readPin(uint8_t pin) {
  if (pin == UINT8_MAX) return 0.0f;
  int raw = analogRead(pin);
  return raw / 4095.0f;
}

bool begin(const Prizm::PotConfig &cfg) {
  sCfg = cfg;
  analogReadResolution(12);
  sReady = true;
  sSmoothed.brightness = readPin(cfg.brightnessPin);
  sSmoothed.fxSpeed = readPin(cfg.fxSpeedPin);
  return true;
}

PotReadings read() {
  if (!sReady) return sSmoothed;
  float alpha = 0.2f;
  float brightness = readPin(sCfg.brightnessPin);
  float fx = readPin(sCfg.fxSpeedPin);

  sSmoothed.brightness = std::clamp(alpha * brightness + (1.0f - alpha) * sSmoothed.brightness, 0.0f, 1.0f);
  sSmoothed.fxSpeed = std::clamp(alpha * fx + (1.0f - alpha) * sSmoothed.fxSpeed, 0.0f, 1.0f);
  return sSmoothed;
}

} // namespace PotControl

