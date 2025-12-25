#pragma once

// =======================
// IAN (ESP32-S3) PIN PLAN
// =======================

// ---- I2C Add-Ons BUS ----
#define IAN_I2C_SDA                8
#define IAN_I2C_SCL                9
#define IAN_I2C_FREQ_HZ            400000
#define IAN_ADDON_INT              21
#define IAN_ADDON_UART_TX          26
#define IAN_ADDON_UART_RX          27

// ---- SPI Flash (W25Qxx) ----
#define IAN_SPI_SCK                12
#define IAN_SPI_MOSI               11
#define IAN_SPI_MISO               13
#define IAN_FLASH_CS               14

// ---- NeoPixel Outputs (RMT) ----
#define IAN_PIXEL_1                4
#define IAN_PIXEL_2                5
#define IAN_PIXEL_3                6
#define IAN_PIXEL_4                7

// ---- Relay / MOSFET Outputs ----
#define IAN_OUT_1                  39
#define IAN_OUT_2                  40
#define IAN_OUT_3                  41
#define IAN_OUT_4                  42

// ---- Spare ADC ----
#define IAN_ADC_1                  1
#define IAN_ADC_2                  2

// ---- Links ----
// IAN <-> SUE wired UART (UART1)
#define IAN_SUE_UART_PORT          1
#define IAN_SUE_UART_BAUD          115200
#define IAN_SUE_UART_TX            17
#define IAN_SUE_UART_RX            18

// IAN <-> KIDS wired UART (UART2)
#define IAN_KIDS_UART_PORT         2
#define IAN_KIDS_UART_BAUD         115200
#define IAN_KIDS_UART_TX           16
#define IAN_KIDS_UART_RX           15

// Debug console fallback
#define IAN_DEBUG_UART_PORT        0
#define IAN_DEBUG_UART_BAUD        115200
#define IAN_DEBUG_UART_TX          43
#define IAN_DEBUG_UART_RX          44

