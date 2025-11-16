#pragma once

#include "../common/DuoFrame.h"

void buttons_begin();
void buttons_update();
void buttons_applyRelayState(uint8_t relayIndex, uint8_t state);
