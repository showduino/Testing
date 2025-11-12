#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <FS.h>
#include <ArduinoJson.h>
#include <array>

namespace Prizm {

// ────────────────────────────────────────────────────────────────
//  VERSIONING
// ────────────────────────────────────────────────────────────────
constexpr const char *kFirmwareVersion = "0.1.0-dev";

// ────────────────────────────────────────────────────────────────
//  DEFAULT CONSTANTS
// ────────────────────────────────────────────────────────────────
constexpr uint16_t kDefaultPixelCount = 300;
constexpr uint16_t kDefaultUniverse = 1;
constexpr uint16_t kDefaultUniverses = 2;    // 2 universes × 512 channels ≈ 340 pixels
constexpr uint16_t kDefaultChannelsPerUniverse = 512;
constexpr bool     kDefaultMulticast = true;
constexpr uint8_t  kDefaultPixelPin = 18;
constexpr uint8_t  kDefaultPixelBrightness = 200;
constexpr uint8_t  kDefaultDMXPin = 17;
constexpr uint16_t kDefaultDMXChannels = 128;
constexpr uint16_t kDefaultDMXFps = 40;
constexpr uint8_t  kDefaultJoystickSda = 9;
constexpr uint8_t  kDefaultJoystickScl = 8;
constexpr uint8_t  kDefaultJoystickInt = 7;
constexpr uint8_t  kDefaultPCA9685Addr = 0x40;
constexpr uint8_t  kDefaultPotBrightness = 1;   // GPIO1 (ADC1_CH0)
constexpr uint8_t  kDefaultPotFxSpeed = 2;      // GPIO2 (ADC1_CH1)
constexpr uint8_t  kDefaultBtnStop = 12;
constexpr uint8_t  kDefaultBtnCycle = 13;
constexpr uint8_t  kDefaultBtnConfirm = 14;
constexpr bool     kDefaultBtnActiveLow = true;
constexpr uint8_t  kDefaultOledSda = 5;
constexpr uint8_t  kDefaultOledScl = 6;
constexpr uint8_t  kDefaultOledAddr = 0x3C;
constexpr uint8_t  kDefaultSDCs = 10;
constexpr uint16_t kDefaultWebPort = 80;
constexpr uint8_t  kMaxUniverses = 12;          // Safety cap (12 × 512 = 6144 channels)

struct NetworkConfig {
  String ssid {"PrizmLink"};
  String password {"prizm1234"};
  String hostname {"prizmlink"};
  bool apFallback {true};
  bool useDHCP {true};
  IPAddress localIp {192, 168, 1, 60};
  IPAddress gateway {192, 168, 1, 1};
  IPAddress subnet {255, 255, 255, 0};
  IPAddress dns {1, 1, 1, 1};
  bool multicast {kDefaultMulticast};
};

struct E131Config {
  uint16_t startUniverse {kDefaultUniverse};
  uint16_t universeCount {kDefaultUniverses};
  uint16_t channelsPerUniverse {kDefaultChannelsPerUniverse};
  uint16_t priority {100};
};

struct PixelConfig {
  bool enabled {true};
  uint16_t count {kDefaultPixelCount};
  uint8_t dataPin {kDefaultPixelPin};
  uint8_t brightness {kDefaultPixelBrightness};
  bool useWhiteChannel {false}; // SK6812
  bool grbwOrder {false};
};

struct DMXConfig {
  bool enabled {true};
  uint16_t channels {kDefaultDMXChannels};
  uint8_t txPin {kDefaultDMXPin};
  uint16_t fps {kDefaultDMXFps};
};

struct ServoConfig {
  bool enabled {true};
  uint8_t pcaAddress {kDefaultPCA9685Addr};
  uint8_t sda {kDefaultJoystickSda};
  uint8_t scl {kDefaultJoystickScl};
  uint8_t joystickXPin {4};
  uint8_t joystickYPin {5};
  uint8_t joystick2XPin {6};
  uint8_t joystick2YPin {7};
  uint8_t button1Pin {35};
  uint8_t button2Pin {36};
  uint8_t button1ActiveState {LOW};
  uint8_t button2ActiveState {LOW};
  float maxServoAngle {180.0f};
  float minServoAngle {0.0f};
  float neutralAngle {90.0f};
};

struct PotConfig {
  uint8_t brightnessPin {kDefaultPotBrightness};
  uint8_t fxSpeedPin {kDefaultPotFxSpeed};
};

struct ButtonConfig {
  uint8_t stopPin {kDefaultBtnStop};
  uint8_t cyclePin {kDefaultBtnCycle};
  uint8_t confirmPin {kDefaultBtnConfirm};
  bool activeLow {kDefaultBtnActiveLow};
};

struct OLEDConfig {
  bool enabled {true};
  uint8_t sda {kDefaultOledSda};
  uint8_t scl {kDefaultOledScl};
  uint8_t address {kDefaultOledAddr};
};

struct SDConfig {
  bool enabled {true};
  bool useSpi {true};
  uint8_t csPin {kDefaultSDCs};
  String root {"/"};
};

struct WebConfig {
  bool enabled {true};
  uint16_t port {kDefaultWebPort};
  bool websocket {true};
};

struct FailsafeConfig {
  uint32_t timeoutMs {5000};
  bool enableFx {true};
  String fxPreset {"rainbow"};
  uint8_t brightnessFloor {32};
};

struct PrizmConfig {
  NetworkConfig network;
  E131Config e131;
  PixelConfig pixels;
  DMXConfig dmx;
  ServoConfig servos;
  PotConfig pots;
  ButtonConfig buttons;
  OLEDConfig oled;
  SDConfig sd;
  WebConfig web;
  FailsafeConfig failsafe;
};

struct RuntimeStats {
  uint32_t lastPacketMs {0};
  uint32_t packetCounter {0};
  float fps {0.0f};
  float cpu0Load {0.0f};
  float cpu1Load {0.0f};
  uint32_t lastLogMs {0};
  uint32_t lastWebsocketMs {0};
  bool networkActive {false};
  bool manualOverride {false};
};

namespace Config {

extern PrizmConfig active;
extern RuntimeStats stats;

void applyDefaults(PrizmConfig &cfg);

bool load(fs::FS &fs, const char *path = "/config.json");
bool save(fs::FS &fs, const char *path = "/config.json");

String toJsonString(const PrizmConfig &cfg, bool pretty = false);

} // namespace Config

} // namespace Prizm

