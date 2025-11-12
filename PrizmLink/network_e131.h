#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <vector>
#include "config.h"

namespace NetworkE131 {

struct PacketInfo {
  uint16_t universe {0};
  size_t length {0};
  uint32_t sequence {0};
  uint32_t timestampMs {0};
};

bool begin(const Prizm::PrizmConfig &cfg);
void loop();

bool hasData();
const uint8_t *pixelData(size_t &length);
const uint8_t *dmxData(size_t &length);

PacketInfo lastPacket();

float fps();

void setManualOverride(bool enabled);
bool manualOverride();

bool isNetworkActive();
uint32_t lastPacketMs();

} // namespace NetworkE131

