#pragma once

#include <Arduino.h>

void addons_begin();
void addons_update();
void addons_handleRelay(uint8_t relayIndex, uint8_t state);
