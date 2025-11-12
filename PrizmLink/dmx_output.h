#pragma once

#include <Arduino.h>
#include "config.h"

namespace DMXOutput {

bool begin(const Prizm::DMXConfig &cfg);
void loop();
void update(const uint8_t *data, size_t length);
void blackout();

bool isReady();

} // namespace DMXOutput

