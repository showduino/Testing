#pragma once

// =======================
// KIDS (ESP32-C3) PIN PLAN
// =======================

// ---- Parent UART (wired) ----
#define KID_PARENT_UART_PORT       0
#define KID_PARENT_UART_BAUD       115200
#define KID_PARENT_UART_TX         21
#define KID_PARENT_UART_RX         20

// ---- Indicators ----
#define KID_STATUS_LED             7   // external LED recommended

// ---- Sensors ----
#define KID_SENSOR_1               4   // ADC-capable
#define KID_SENSOR_2               5   // ADC-capable

// ---- Small Output ----
#define KID_SMALL_OUT              6   // NeoPixel (RMT) or PWM (LEDC)

