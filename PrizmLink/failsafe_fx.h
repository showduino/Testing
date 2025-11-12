#pragma once

#include <Arduino.h>
#include <FastLED.h>

namespace FailsafeFX {

void begin(uint16_t pixelCount);
void render(CRGB *leds, uint16_t count, uint32_t nowMs, float brightnessScalar);
void setSpeed(float speedScalar);

} // namespace FailsafeFX

