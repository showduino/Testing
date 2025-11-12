#include "failsafe_fx.h"
#include <algorithm>

namespace FailsafeFX {

static uint16_t sCount = 0;
static float sSpeedScalar = 1.0f;

void begin(uint16_t pixelCount) {
  sCount = pixelCount;
}

void setSpeed(float speedScalar) {
  sSpeedScalar = speedScalar;
}

void render(CRGB *leds, uint16_t count, uint32_t nowMs, float brightnessScalar) {
  if (!leds || count == 0) return;

  float t = (nowMs / 1000.0f) * sSpeedScalar;
  const float speed = 0.5f;
  for (uint16_t i = 0; i < count; ++i) {
    float offset = (static_cast<float>(i) / std::max<uint16_t>(count, 1)) * 2.0f * PI;
    float wave = (sin(t * speed + offset) + 1.0f) * 0.5f;
    uint8_t intensity = static_cast<uint8_t>(constrain(wave * 255.0f * brightnessScalar, 0.0f, 255.0f));
    leds[i] = CHSV((nowMs / 32 + i * 2) % 255, 255, intensity);
  }
}

} // namespace FailsafeFX

