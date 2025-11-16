#pragma once

#include <Arduino.h>

void pixels_begin();
void pixels_update();

void pixels_enableTimeCircuitFlicker(bool enabled);
void pixels_enableConsoleFlicker(bool enabled);
void pixels_enableTimeDisplayFlicker(bool enabled);
void pixels_enableMachineTwinkle(bool enabled);
void pixels_enableCandleFlicker(bool enabled);

void pixels_triggerShockPulse();

void pixels_showTwentyFive();
void pixels_showFiveTen();
void pixels_showZero();
void pixels_showNineFourTwo();
void pixels_showOneEightFourTwo();

void pixels_consoleFadeIn();
void pixels_consoleFadeOut();

// Utility helpers exposed for timeline scripting
void pixels_setTimeDisplayRaw(uint16_t index, uint32_t color);
void pixels_commitTimeDisplay();
