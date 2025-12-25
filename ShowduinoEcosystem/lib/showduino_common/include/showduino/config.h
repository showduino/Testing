#pragma once

#include <stdint.h>

// ============================
// Showduino node identity config
// ============================

// Instance indices (override via build flags if needed)
#ifndef SHOWDUINO_IAN_INDEX
#define SHOWDUINO_IAN_INDEX 1
#endif

#ifndef SHOWDUINO_KID_INDEX
#define SHOWDUINO_KID_INDEX 1
#endif

// Numeric node IDs (stable across transports)
// UI  : 1
// SUE : 2
// IAN : 100 + index
// KID : 200 + index
constexpr uint16_t SHOWDUINO_NODEID_UI  = 1;
constexpr uint16_t SHOWDUINO_NODEID_SUE = 2;
constexpr uint16_t SHOWDUINO_NODEID_IAN_BASE = 100;
constexpr uint16_t SHOWDUINO_NODEID_KID_BASE = 200;

// Determine this firmware's node ID
#if defined(SHOWDUINO_NODE_UI)
constexpr uint16_t SHOWDUINO_NODE_ID = SHOWDUINO_NODEID_UI;
#elif defined(SHOWDUINO_NODE_SUE)
constexpr uint16_t SHOWDUINO_NODE_ID = SHOWDUINO_NODEID_SUE;
#elif defined(SHOWDUINO_NODE_IAN)
constexpr uint16_t SHOWDUINO_NODE_ID = static_cast<uint16_t>(SHOWDUINO_NODEID_IAN_BASE + SHOWDUINO_IAN_INDEX);
#elif defined(SHOWDUINO_NODE_KIDS)
constexpr uint16_t SHOWDUINO_NODE_ID = static_cast<uint16_t>(SHOWDUINO_NODEID_KID_BASE + SHOWDUINO_KID_INDEX);
#else
constexpr uint16_t SHOWDUINO_NODE_ID = 0;
#endif

// Link-layer practical maximums (bytes of COBS-encoded payload, excluding UART delimiter)
constexpr uint16_t SHOWDUINO_MAX_FRAME_ESPNOW = 240;   // keep under ESP-NOW limit
constexpr uint16_t SHOWDUINO_MAX_FRAME_UART   = 1024;  // conservative for serial

