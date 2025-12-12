#pragma once

#include <stdint.h>

// Must match WLED ShowduinoBridge usermod protocol.
static constexpr uint8_t SDP_MAGIC[4] = {'S','D','P','1'};
static constexpr uint8_t SDP_TYPE_HELLO   = 0x01;
static constexpr uint8_t SDP_TYPE_CMDJSON = 0x02;
static constexpr uint8_t SDP_TYPE_SENSOR  = 0x03;

// Frame layout (all little-endian where applicable):
// Header:
//   magic[4] = "SDP1"
//   pairCode u32 LE
//   type u8
//
// HELLO:
//   leds u16 LE
//   nameLen u8
//   name[nameLen] (ASCII/UTF-8, max 16)
//
// CMDJSON:
//   jsonLen u8
//   json[jsonLen]
//
// SENSOR:
//   ldr u16 LE
//   button u8
//   mp3_playing u8
//   mp3_track u16 LE
//   mp3_vol u8

