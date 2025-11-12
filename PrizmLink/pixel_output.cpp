#include <algorithm>
#include "pixel_output.h"
#include "debug_utils.h"
#include "failsafe_fx.h"

namespace PixelOutput {

static bool sReady = false;
static uint16_t sPixelCount = 0;
static CRGB *sLeds = nullptr;
static uint8_t sBaseBrightness = 255;
static bool sHasWhite = false;

bool begin(const Prizm::PixelConfig &cfg) {
  if (!cfg.enabled) {
    Debug::warn("PIX", "Pixel output disabled via config");
    sReady = false;
    return false;
  }

  sPixelCount = cfg.count;
  sHasWhite = cfg.useWhiteChannel;
  sBaseBrightness = cfg.brightness;

  if (!sLeds) {
    sLeds = static_cast<CRGB*>(malloc(sizeof(CRGB) * sPixelCount));
  }

  if (!sLeds) {
    Debug::error("PIX", "Failed to alloc %u pixels", sPixelCount);
    sReady = false;
    return false;
  }

  if (sHasWhite) {
    auto &controller = FastLED.addLeds<SK6812, 0, GRBW>(sLeds, sPixelCount);
    controller.setPin(cfg.dataPin);
  } else {
    auto &controller = FastLED.addLeds<WS2812B, 0, GRB>(sLeds, sPixelCount);
    controller.setPin(cfg.dataPin);
  }
  FastLED.setBrightness(sBaseBrightness);
  FastLED.clear();
  FastLED.show();

  Debug::info("PIX", "Configured %u pixels", sPixelCount);
  sReady = true;
  return true;
}

void updateFromE131(const uint8_t *data, size_t length, float brightnessScalar) {
  if (!sReady || !data) return;

  size_t stride = sHasWhite ? 4 : 3;
  size_t expectedBytes = static_cast<size_t>(sPixelCount) * stride;
  size_t copyBytes = std::min(length, expectedBytes);
  size_t pixels = copyBytes / stride;

  for (size_t i = 0; i < pixels; ++i) {
    size_t base = i * stride;
    uint8_t r = data[base];
    uint8_t g = data[base + 1];
    uint8_t b = data[base + 2];
    if (sHasWhite && base + 3 < length) {
      uint8_t w = data[base + 3];
      r = std::min<int>(255, r + w);
      g = std::min<int>(255, g + w);
      b = std::min<int>(255, b + w);
    }
    sLeds[i] = CRGB(r, g, b);
  }

  uint8_t brightness = constrain(static_cast<int>(sBaseBrightness * brightnessScalar), 0, 255);
  FastLED.setBrightness(brightness);
  FastLED.show();
}

void applyFailsafe(float brightnessScalar, uint32_t nowMs) {
  if (!sReady) return;
  FailsafeFX::render(sLeds, sPixelCount, nowMs, brightnessScalar);
  FastLED.show();
}

void blackout() {
  if (!sReady) return;
  FastLED.clear();
  FastLED.show();
}

void loop() {
  if (!sReady) return;
  FastLED.delay(0);
}

bool isReady() { return sReady; }
uint16_t pixelCount() { return sPixelCount; }

} // namespace PixelOutput

