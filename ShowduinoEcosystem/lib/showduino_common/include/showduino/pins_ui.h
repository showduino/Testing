#pragma once

// ===========================================
// UI (JC8048W550 ESP32-S3) INTEGRATION PINS
// (Touch + UI SD + optional wired link only)
// ===========================================

// ---- GT911 Touch ----
#define UI_TOUCH_SDA               19
#define UI_TOUCH_SCL               20
#define UI_TOUCH_INT               38
#define UI_TOUCH_RST               10
#define UI_TOUCH_I2C_FREQ_HZ       400000

// NOTE: GPIO19/20 are used by touch on this module -> native USB is NOT available.

// ---- UI SD Card (SPI) ----
#define UI_SPI_SCK                 12
#define UI_SPI_MOSI                11
#define UI_SPI_MISO                13
#define UI_SD_CS                   17

// ---- Optional wired UI <-> SUE UART ----
#define UI_SUE_UART_PORT           2
#define UI_SUE_UART_BAUD           115200
#define UI_SUE_UART_TX             33
#define UI_SUE_UART_RX             34

// ---- Debug (UI) ----
#define UI_DEBUG_UART_PORT         0
#define UI_DEBUG_UART_BAUD         115200
#define UI_DEBUG_UART_TX           43
#define UI_DEBUG_UART_RX           44

