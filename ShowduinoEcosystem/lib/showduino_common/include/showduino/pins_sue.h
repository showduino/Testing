#pragma once

// =======================
// SUE (ESP32-S3) PIN PLAN
// =======================

// ---- I2C Add-Ons BUS ----
#define SUE_I2C_SDA                8
#define SUE_I2C_SCL                9
#define SUE_I2C_FREQ_HZ            400000
#define SUE_ADDON_INT              21
#define SUE_ADDON_UART_TX          26
#define SUE_ADDON_UART_RX          27

// ---- Shared SPI (SD + W25Qxx) ----
#define SUE_SPI_SCK                12
#define SUE_SPI_MOSI               11
#define SUE_SPI_MISO               13

#define SUE_SD_CS                  10
#define SUE_FLASH_CS               14

// ---- Power / IO ----
#define SUE_VOLTAGE_ADC            1   // ADC1: use resistor divider + calibration
#define SUE_MOSFET_PWM             7   // LEDC-capable PWM pin

// ---- Links ----
// SUE <-> IAN wired UART (UART1 recommended)
#define SUE_IAN_UART_PORT          1
#define SUE_IAN_UART_BAUD          115200
#define SUE_IAN_UART_TX            18
#define SUE_IAN_UART_RX            17

// Debug console options (UART0 pins vary by board; keep as fallback)
#define SUE_DEBUG_UART_PORT        0
#define SUE_DEBUG_UART_BAUD        115200
#define SUE_DEBUG_UART_TX          43
#define SUE_DEBUG_UART_RX          44

