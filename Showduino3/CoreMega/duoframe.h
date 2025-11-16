#pragma once

#include <Arduino.h>

#include "../common/DuoFrame.h"

namespace duoframe {

using FrameCallback = void (*)(const showduino::DuoFrame &frame);

void begin(HardwareSerial &serialPort, uint32_t baud, uint8_t rxPin, uint8_t txPin, FrameCallback cb);
void poll();
bool send(showduino::DuoFrameCommand cmd, const uint8_t *payload, uint8_t length);
bool sendFrame(const showduino::DuoFrame &frame);

}  // namespace duoframe
