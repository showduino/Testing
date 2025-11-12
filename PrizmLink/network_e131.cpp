#include <cstring>
#include <algorithm>
#include "network_e131.h"
#include "debug_utils.h"

namespace NetworkE131 {

static WiFiUDP sUdp;
static bool sWiFiConnected = false;
static bool sActive = false;
static bool sManualOverride = false;
static uint32_t sLastPacketMs = 0;
static uint32_t sPacketCounter = 0;
static uint32_t sLastFpsUpdateMs = 0;
static float sFps = 0.0f;
static uint16_t sUniverseBase = 0;
static uint16_t sUniverseCount = 0;
static std::vector<uint8_t> sPixelBuffer;
static std::vector<uint8_t> sDMXBuffer;
static PacketInfo sLastPacketInfo {};

constexpr uint16_t kE131Port = 5568;

static bool isValidE131(const uint8_t *data, size_t len, PacketInfo &info) {
  if (len < 126) return false; // minimal root + framing + DMP headers

  uint16_t preamble = (data[0] << 8) | data[1];
  if (preamble != 0x0010) return false;

  const char *cid = reinterpret_cast<const char*>(&data[4]);
  if (strncmp(cid, "ASC-E1.17", 9) != 0) return false;

  uint16_t framingFlagsLength = (data[38] << 8) | data[39];
  if ((framingFlagsLength & 0x7000) != 0x7000) return false;

  uint16_t vector = (data[40] << 8) | data[41];
  if (vector != 0x00002) return false; // E1.31 Data Packet

  uint16_t universe = (data[113] << 8) | data[114];
  info.universe = universe;

  uint8_t sequence = data[111];
  info.sequence = sequence;

  const uint8_t *dmp = &data[115];
  uint8_t dmpVector = dmp[0];
  if (dmpVector != 0x02) return false;
  uint8_t addrType = dmp[1];
  if (addrType != 0xa1) return false;

  uint16_t propValCount = (dmp[3] << 8) | dmp[4];
  if (propValCount < 2) return false;

  size_t pixelLen = propValCount - 1; // first is DMX start code
  if (pixelLen > sPixelBuffer.size()) {
    // auto-grow to support config
    sPixelBuffer.resize(pixelLen);
  }
  size_t dmxLen = std::min<size_t>(pixelLen, sDMXBuffer.size());

  info.length = pixelLen;
  info.timestampMs = millis();

  memcpy(sPixelBuffer.data(), &dmp[5], std::min<size_t>(pixelLen, sPixelBuffer.size()));
  memcpy(sDMXBuffer.data(), &dmp[5], dmxLen);

  return true;
}

static void connectWiFi(const Prizm::PrizmConfig &cfg) {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(cfg.network.hostname.c_str());

  if (!cfg.network.useDHCP) {
    WiFi.config(cfg.network.localIp, cfg.network.gateway, cfg.network.subnet, cfg.network.dns);
  }

  Debug::info("WiFi", "Connecting to %s", cfg.network.ssid.c_str());
  WiFi.begin(cfg.network.ssid.c_str(), cfg.network.password.c_str());

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(200);
  }

  if (WiFi.status() == WL_CONNECTED) {
    sWiFiConnected = true;
    Debug::info("WiFi", "Connected, IP=%s", WiFi.localIP().toString().c_str());
  } else if (cfg.network.apFallback) {
    Debug::warn("WiFi", "Station connect failed, starting AP");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(cfg.network.hostname.c_str(), cfg.network.password.c_str());
    sWiFiConnected = true;
  } else {
    Debug::error("WiFi", "Failed to connect");
  }
}

bool begin(const Prizm::PrizmConfig &cfg) {
  sUniverseBase = cfg.e131.startUniverse;
  sUniverseCount = cfg.e131.universeCount;
  sPixelBuffer.assign(cfg.pixels.count * (cfg.pixels.useWhiteChannel ? 4 : 3), 0);
  sDMXBuffer.assign(cfg.dmx.channels, 0);

  connectWiFi(cfg);

  if (!sWiFiConnected) return false;

  bool bound = false;
  if (cfg.network.multicast) {
    IPAddress group(239,
                    255,
                    (cfg.e131.startUniverse >> 8) & 0xFF,
                    cfg.e131.startUniverse & 0xFF);
    bound = sUdp.beginMulticast(WiFi.localIP(), group, kE131Port);
    if (bound) {
      Debug::info("E131", "Joined multicast %s", group.toString().c_str());
    } else {
      Debug::warn("E131", "Multicast bind failed, falling back to unicast");
    }
  }
  if (!bound) {
    bound = sUdp.begin(kE131Port);
  }
  if (!bound) {
    Debug::error("E131", "UDP bind failed");
    return false;
  }

  Debug::info("E131", "Listening on port %d", kE131Port);
  sLastPacketMs = millis();
  sLastFpsUpdateMs = sLastPacketMs;

  return true;
}

void loop() {
  if (!sWiFiConnected) return;

  int packetSize = sUdp.parsePacket();
  if (packetSize <= 0) {
    sActive = (millis() - sLastPacketMs) < Prizm::Config::active.failsafe.timeoutMs;
    return;
  }

  static std::vector<uint8_t> buffer(1500);
  if (packetSize > static_cast<int>(buffer.size())) {
    buffer.resize(packetSize);
  }

  int len = sUdp.read(buffer.data(), buffer.size());
  if (len <= 0) return;

  PacketInfo info;
  if (!isValidE131(buffer.data(), len, info)) {
    return;
  }

  if (info.universe < sUniverseBase || info.universe >= sUniverseBase + sUniverseCount) {
    return; // not in configured range
  }

  sLastPacketInfo = info;
  sLastPacketMs = info.timestampMs;
  sPacketCounter++;
  sActive = true;

  uint32_t now = millis();
  if (now - sLastFpsUpdateMs >= 1000) {
    sFps = (1000.0f * sPacketCounter) / (now - sLastFpsUpdateMs);
    sPacketCounter = 0;
    sLastFpsUpdateMs = now;
  }
}

bool hasData() {
  return sActive && !sManualOverride;
}

const uint8_t *pixelData(size_t &length) {
  length = sLastPacketInfo.length;
  return sPixelBuffer.data();
}

const uint8_t *dmxData(size_t &length) {
  length = std::min<size_t>(sLastPacketInfo.length, sDMXBuffer.size());
  return sDMXBuffer.data();
}

PacketInfo lastPacket() {
  return sLastPacketInfo;
}

float fps() {
  return sFps;
}

void setManualOverride(bool enabled) {
  sManualOverride = enabled;
}

bool manualOverride() {
  return sManualOverride;
}

bool isNetworkActive() {
  return sActive;
}

uint32_t lastPacketMs() {
  return sLastPacketMs;
}

} // namespace NetworkE131

