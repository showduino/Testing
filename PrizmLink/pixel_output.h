#pragma once

#include <Arduino.h>
#include <FastLED.h>
#include "config.h"

namespace PixelOutput {

bool begin(const Prizm::PixelConfig &cfg);
void updateFromE131(const uint8_t *data, size_t length, float brightnessScalar = 1.0f);
void applyFailsafe(float brightnessScalar, uint32_t nowMs);
void blackout();

void loop();

bool isReady();
uint16_t pixelCount();

} // namespace PixelOutput

