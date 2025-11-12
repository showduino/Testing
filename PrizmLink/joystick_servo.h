#pragma once

#include <Arduino.h>
#include <Adafruit_PWMServoDriver.h>
#include "config.h"

namespace JoystickServo {

struct ServoState {
  float current[4] {0};
  float target[4] {0};
  bool manual {false};
};

bool begin(const Prizm::ServoConfig &cfg);
void update(float brightnessScalar, float speedScalar);
void setNetworkTargets(const float *angles, size_t count);
ServoState state();

void setManualOverride(bool enabled);

} // namespace JoystickServo

