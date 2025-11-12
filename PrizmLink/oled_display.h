#pragma once

#include <Arduino.h>
#include <Adafruit_SSD1306.h>
#include "config.h"
#include "network_e131.h"

namespace OLEDDisplay {

bool begin(const Prizm::OLEDConfig &cfg);
void update(const Prizm::RuntimeStats &stats, const Prizm::PrizmConfig &cfg);
void showEmergency();

} // namespace OLEDDisplay

