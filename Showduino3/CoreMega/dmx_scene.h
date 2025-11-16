#pragma once

#include <Arduino.h>

void dmx_scene_begin();
void dmx_scene_seek(uint32_t positionMs);
uint32_t dmx_scene_length();
