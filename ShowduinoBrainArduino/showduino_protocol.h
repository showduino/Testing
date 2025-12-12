#pragma once

#include <stdint.h>

// Must match ShowduinoProp + WLED ShowduinoBridge protocol.
static constexpr uint8_t SDP_MAGIC[4] = {'S','D','P','1'};
static constexpr uint8_t SDP_TYPE_HELLO   = 0x01;
static constexpr uint8_t SDP_TYPE_CMDJSON = 0x02;
static constexpr uint8_t SDP_TYPE_SENSOR  = 0x03;

