#pragma once

#include <Arduino.h>

// Button matrix (2x 74HC4067)
constexpr uint8_t BUTTON_COUNT = 32;
constexpr uint8_t MUX_SELECT_PINS[4] = {22, 23, 24, 25};
constexpr uint8_t MUX_SIG_A = A0;
constexpr uint8_t MUX_SIG_B = A1;
constexpr bool BUTTON_ACTIVE_LOW = true;
constexpr uint16_t BUTTON_DEBOUNCE_MS = 35;
constexpr uint16_t BUTTON_LONGPRESS_MS = 650;

// DuoFrame serial link
constexpr uint32_t DUO_SERIAL_BAUD = 115200;
constexpr uint8_t DUO_SERIAL_PORT = 1;  // Serial1
constexpr uint8_t DUO_SERIAL_RX = 19;
constexpr uint8_t DUO_SERIAL_TX = 18;

// Heartbeat + status
constexpr uint32_t HEARTBEAT_INTERVAL_MS = 500;
constexpr uint32_t STATUS_INTERVAL_MS = 1000;

// Emergency input
constexpr uint8_t EMERGENCY_PIN = 4;
constexpr bool EMERGENCY_ACTIVE_LOW = true;

// I2C bus for add-ons
constexpr uint8_t ADDON_I2C_SDA = SDA;
constexpr uint8_t ADDON_I2C_SCL = SCL;
constexpr uint32_t ADDON_I2C_FREQ = 400000;
