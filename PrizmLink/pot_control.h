#pragma once

#include <Arduino.h>
#include "config.h"

namespace PotControl {

struct PotReadings {
  float brightness {1.0f};
  float fxSpeed {1.0f};
};

bool begin(const Prizm::PotConfig &cfg);
PotReadings read();

} // namespace PotControl

