#pragma once

#include <Arduino.h>

#include "../common/DuoFrame.h"

void dmx_begin();
void dmx_handleCommand(const showduino::DuoFrame &frame);
void dmx_blackout();
