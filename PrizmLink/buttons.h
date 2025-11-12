#pragma once

#include <Arduino.h>
#include "config.h"

namespace Buttons {

enum class Event : uint8_t {
  None,
  EmergencyStop,
  CycleMode,
  Confirm
};

bool begin(const Prizm::ButtonConfig &cfg);
Event poll();

bool emergencyLatched();
void clearEmergency();

} // namespace Buttons

