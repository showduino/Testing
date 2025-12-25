#pragma once

// Board constraints (authoritative reference)
// ==========================================
//
// ESP32-S3
// --------
// Strapping / boot pins to treat as "hands off" for external circuitry:
// - GPIO0  : BOOT strap (many dev boards have BOOT button)
// - GPIO45 : strap (avoid external pulls/driving)
// - GPIO46 : strap (avoid external pulls/driving)
// - GPIO3  : strap-related behavior; avoid for critical signals
//
// Native USB (USB-OTG / USB-CDC):
// - GPIO19 = USB D-
// - GPIO20 = USB D+
//
// Policy:
// - On SUE/IAN we reserve GPIO19/20 for native USB when enabled.
// - On JC8048W550 UI module GPIO19/20 are used by GT911 touch I2C, so native USB is NOT available.
//
//
// ESP32-C3
// --------
// Strapping / boot pins to avoid for "must boot" signals:
// - GPIO9 : BOOT strap (BOOT button on many C3 boards)
// - GPIO8 : strap (often also onboard LED on SuperMini variants)
// - GPIO2 : strap (avoid strong pulls)
//
// Native USB Serial/JTAG:
// - GPIO18 = USB D-
// - GPIO19 = USB D+
//
// Policy:
// - KIDS pin plan avoids GPIO18/19 and strap pins 2/8/9.

