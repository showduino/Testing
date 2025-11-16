#pragma once

#include <Arduino.h>

#include "../common/DuoFrame.h"

void audio_begin();
void audio_handleCommand(const showduino::DuoFrame &frame);
