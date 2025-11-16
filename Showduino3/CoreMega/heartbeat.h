#pragma once

#include "../common/DuoFrame.h"

void heartbeat_begin();
void heartbeat_update();
void heartbeat_handleFrame(const showduino::DuoFrame &frame);
