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

// MP3 player pins (Dual YX5300 serial modules)
constexpr uint8_t MP3_AMBIENT_TX = 12;
constexpr uint8_t MP3_AMBIENT_RX = 11;
constexpr uint8_t MP3_MACHINE_TX = 38;
constexpr uint8_t MP3_MACHINE_RX = 37;
constexpr uint32_t MP3_SERIAL_BAUD = 9600;

// NeoPixel strip definitions
constexpr uint8_t NEOPIXEL_MACHINE_PIN = 22;
constexpr uint16_t NEOPIXEL_MACHINE_COUNT = 100;

constexpr uint8_t NEOPIXEL_TIME_DISPLAY_PIN = 23;
constexpr uint16_t NEOPIXEL_TIME_DISPLAY_COUNT = 100;

constexpr uint8_t NEOPIXEL_CANDLE_PIN = 24;
constexpr uint8_t NEOPIXEL_CANDLE_COUNT = 9;  // 3 candles * 3 pixels

constexpr uint8_t NEOPIXEL_TIME_CIRCUITS_PIN = 30;
constexpr uint16_t NEOPIXEL_TIME_CIRCUITS_COUNT = 100;

constexpr uint8_t NEOPIXEL_INDICATOR_PIN = 31;
constexpr uint16_t NEOPIXEL_INDICATOR_COUNT = 100;

// Buzzer for cute sounds
constexpr uint8_t BUZZER_PIN = 36;
