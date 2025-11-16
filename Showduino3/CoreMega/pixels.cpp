#include "pixels.h"

#include <Adafruit_NeoPixel.h>
#include <initializer_list>

#include "audio.h"
#include "config.h"

namespace {

Adafruit_NeoPixel machineStrip(NEOPIXEL_MACHINE_COUNT, NEOPIXEL_MACHINE_PIN,
                               NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel timeDisplayStrip(NEOPIXEL_TIME_DISPLAY_COUNT, NEOPIXEL_TIME_DISPLAY_PIN,
                                   NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel candleStrip(NEOPIXEL_CANDLE_COUNT, NEOPIXEL_CANDLE_PIN,
                              NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel timeCircuitsStrip(NEOPIXEL_TIME_CIRCUITS_COUNT, NEOPIXEL_TIME_CIRCUITS_PIN,
                                    NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel indicatorStrip(NEOPIXEL_INDICATOR_COUNT, NEOPIXEL_INDICATOR_PIN,
                                 NEO_GRB + NEO_KHZ800);

struct EffectFlags {
  bool timeCircuitFlicker = true;
  bool consoleFlicker = true;
  bool timeDisplayFlicker = true;
  bool machineTwinkle = true;
  bool candleFlicker = true;
} effectFlags;

constexpr uint16_t kTwinkleInterval = 150;
constexpr uint16_t kConsoleFlickerInterval = 25;
constexpr uint16_t kTimeDisplayFlickerInterval = 20;
constexpr uint16_t kTimeCircuitInterval = 500;
constexpr uint16_t kCandleInterval = 80;
constexpr uint16_t kShockInterval = 70;

unsigned long lastTwinkle = 0;
unsigned long lastConsoleFlicker = 0;
unsigned long lastTimeDisplayFlicker = 0;
unsigned long lastTimeCircuit = 0;
unsigned long lastCandleUpdate = 0;

bool shockActive = false;
unsigned long shockStarted = 0;
unsigned long lastShockPulse = 0;

uint32_t colorWhite = Adafruit_NeoPixel::Color(255, 255, 255);
uint32_t colorRed = Adafruit_NeoPixel::Color(255, 0, 0);

void randomizeStrip(Adafruit_NeoPixel &strip) {
  for (uint16_t i = 0; i < strip.numPixels(); ++i) {
    strip.setPixelColor(i, strip.Color(random(256), random(256), random(256)));
  }
  strip.show();
}

void runTimeCircuitFlicker(unsigned long now) {
  if (!effectFlags.timeCircuitFlicker) return;
  if (now - lastTimeCircuit < kTimeCircuitInterval) return;
  lastTimeCircuit = now;
  randomizeStrip(timeCircuitsStrip);
}

void runConsoleFlicker(unsigned long now) {
  if (!effectFlags.consoleFlicker) return;
  if (now - lastConsoleFlicker < kConsoleFlickerInterval) return;
  lastConsoleFlicker = now;
  for (int i = 11; i <= 14 && i < machineStrip.numPixels(); ++i) {
    bool on = random(10) == 0;
    machineStrip.setPixelColor(i, on ? colorWhite : 0);
  }
  machineStrip.show();
}

void runMachineTwinkle(unsigned long now) {
  if (!effectFlags.machineTwinkle) return;
  if (now - lastTwinkle < kTwinkleInterval) return;
  lastTwinkle = now;
  for (int i = 15; i < 20 && i < machineStrip.numPixels(); ++i) {
    bool on = random(10) == 0;
    machineStrip.setPixelColor(i, on ? colorWhite : 0);
  }
  machineStrip.show();
}

void runTimeDisplayFlicker(unsigned long now) {
  if (!effectFlags.timeDisplayFlicker) return;
  if (now - lastTimeDisplayFlicker < kTimeDisplayFlickerInterval) return;
  lastTimeDisplayFlicker = now;
  for (int i = 0; i <= 30 && i < timeDisplayStrip.numPixels(); ++i) {
    bool on = random(10) == 0;
    timeDisplayStrip.setPixelColor(i, on ? colorWhite : 0);
  }
  timeDisplayStrip.show();
}

void runCandleFlicker(unsigned long now) {
  if (!effectFlags.candleFlicker) return;
  if (now - lastCandleUpdate < kCandleInterval) return;
  lastCandleUpdate = now;
  for (uint16_t i = 0; i < candleStrip.numPixels(); ++i) {
    uint8_t r = random(150, 255);
    uint8_t g = random(20, 120);
    uint8_t b = random(0, 30);
    candleStrip.setPixelColor(i, candleStrip.Color(r, g, b));
  }
  candleStrip.show();
}

void runShockEffect(unsigned long now) {
  if (!shockActive) return;
  if (now - lastShockPulse < kShockInterval) return;

  lastShockPulse = now;

  bool on = ((now - shockStarted) / kShockInterval) % 2 == 0;
  uint32_t color = on ? colorWhite : 0;

  for (int i = 0; i < 50 && i < timeDisplayStrip.numPixels(); ++i) {
    timeDisplayStrip.setPixelColor(i, color);
  }
  for (int i = 0; i < 50 && i < machineStrip.numPixels(); ++i) {
    machineStrip.setPixelColor(i, color);
  }
  timeDisplayStrip.show();
  machineStrip.show();

  if (now - shockStarted > 1500) {
    shockActive = false;
    timeDisplayStrip.clear();
    machineStrip.clear();
    timeDisplayStrip.show();
    machineStrip.show();
  }
}

void setIndicesRed(std::initializer_list<uint16_t> indices) {
  for (uint16_t idx : indices) {
    if (idx < timeDisplayStrip.numPixels()) {
      timeDisplayStrip.setPixelColor(idx, colorRed);
    }
  }
  timeDisplayStrip.show();
}

}  // namespace

void pixels_begin() {
  machineStrip.begin();
  machineStrip.show();
  timeDisplayStrip.begin();
  timeDisplayStrip.show();
  candleStrip.begin();
  candleStrip.show();
  timeCircuitsStrip.begin();
  timeCircuitsStrip.show();
  indicatorStrip.begin();
  indicatorStrip.show();
  randomSeed(analogRead(A2));
}

void pixels_update() {
  unsigned long now = millis();
  runTimeCircuitFlicker(now);
  runConsoleFlicker(now);
  runMachineTwinkle(now);
  runTimeDisplayFlicker(now);
  runCandleFlicker(now);
  runShockEffect(now);
}

void pixels_enableTimeCircuitFlicker(bool enabled) { effectFlags.timeCircuitFlicker = enabled; }

void pixels_enableConsoleFlicker(bool enabled) { effectFlags.consoleFlicker = enabled; }

void pixels_enableTimeDisplayFlicker(bool enabled) { effectFlags.timeDisplayFlicker = enabled; }

void pixels_enableMachineTwinkle(bool enabled) { effectFlags.machineTwinkle = enabled; }

void pixels_enableCandleFlicker(bool enabled) { effectFlags.candleFlicker = enabled; }

void pixels_triggerShockPulse() {
  shockActive = true;
  shockStarted = millis();
  lastShockPulse = 0;
  audio_playMachineTrack(21);  // Electric effect track per legacy mapping
}

void pixels_showTwentyFive() {
  timeDisplayStrip.clear();
  setIndicesRed({1, 2, 4, 5, 6, 7, 8, 10, 11, 13, 14, 15, 16, 17, 18, 19, 21, 22, 24, 25, 27});
}

void pixels_showFiveTen() {
  timeDisplayStrip.clear();
  setIndicesRed({0, 1, 2, 3, 4, 5, 7, 12, 14, 15, 16, 17, 18, 19, 22, 23, 25, 26, 27});
}

void pixels_showZero() {
  timeDisplayStrip.clear();
  setIndicesRed({0, 1, 2, 3, 4, 5, 7, 8, 9, 10, 11, 12, 14, 15, 16, 17, 18, 19, 21, 22, 23, 24, 25, 26});
}

void pixels_showNineFourTwo() {
  timeDisplayStrip.clear();
  setIndicesRed({0, 1, 3, 4, 6, 7, 9, 12, 13, 14, 15, 16, 18, 19, 20, 21, 26});
}

void pixels_showOneEightFourTwo() {
  timeDisplayStrip.clear();
  setIndicesRed({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 26});
}

void pixels_consoleFadeIn() {
  for (int brightness = 0; brightness <= 255; ++brightness) {
    for (int i = 11; i <= 17 && i < machineStrip.numPixels(); ++i) {
      machineStrip.setPixelColor(i, machineStrip.Color(brightness, brightness, brightness));
    }
    machineStrip.show();
    delay(70);
  }
}

void pixels_consoleFadeOut() {
  for (int brightness = 255; brightness >= 0; --brightness) {
    for (int i = 11; i <= 17 && i < machineStrip.numPixels(); ++i) {
      machineStrip.setPixelColor(i, machineStrip.Color(brightness, brightness, brightness));
    }
    machineStrip.show();
    delay(70);
  }
}

void pixels_setTimeDisplayRaw(uint16_t index, uint32_t color) {
  if (index >= timeDisplayStrip.numPixels()) return;
  timeDisplayStrip.setPixelColor(index, color);
}

void pixels_commitTimeDisplay() { timeDisplayStrip.show(); }
