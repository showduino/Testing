#include <SD.h>
#include <algorithm>
#include <Arduino.h>
#include "config.h"
#include "debug_utils.h"
#include "sd_logger.h"
#include "network_e131.h"
#include "pixel_output.h"
#include "dmx_output.h"
#include "failsafe_fx.h"
#include "pot_control.h"
#include "buttons.h"
#include "joystick_servo.h"
#include "oled_display.h"
#include "web_server.h"

using namespace Prizm;

static SDLogger::Options loggerOpts;
static PotControl::PotReadings potValues;
static bool failsafeActive = false;
static bool emergencyStop = false;

static void initLogger() {
  loggerOpts.enabled = Config::active.sd.enabled;
  loggerOpts.csPin = Config::active.sd.csPin;
  loggerOpts.bootPrefix = "/logs/boot_";
  loggerOpts.runPrefix = "/logs/run_";
  if (!SDLogger::begin(loggerOpts)) {
    Debug::warn("BOOT", "SD logger unavailable");
    Debug::setSDMirror(false);
  }
}

static void initConfig() {
  if (SDLogger::isReady()) {
    if (!Config::load(SD, "/config.json")) {
      Debug::warn("BOOT", "Using default configuration");
      Config::applyDefaults(Config::active);
    }
  } else {
    Debug::warn("BOOT", "Using default configuration");
    Config::applyDefaults(Config::active);
  }
  Debug::info("BOOT", "Config ready: %s", Config::toJsonString(Config::active, false).c_str());
}

static void initSubsystems() {
  if (Config::active.pixels.enabled) {
    PixelOutput::begin(Config::active.pixels);
    FailsafeFX::begin(Config::active.pixels.count);
  }

  if (Config::active.dmx.enabled) {
    DMXOutput::begin(Config::active.dmx);
  }

  PotControl::begin(Config::active.pots);
  Buttons::begin(Config::active.buttons);
  JoystickServo::begin(Config::active.servos);
  OLEDDisplay::begin(Config::active.oled);

  NetworkE131::begin(Config::active);
  WebServer::begin(Config::active);
}

static void updateStats(bool networkActive) {
  auto &stats = Config::stats;
  stats.networkActive = networkActive;
  stats.manualOverride = NetworkE131::manualOverride();
  stats.lastPacketMs = NetworkE131::lastPacketMs();
  stats.fps = NetworkE131::fps();
  stats.packetCounter = NetworkE131::lastPacket().sequence;
}

static void handleButtons() {
  Buttons::Event ev = Buttons::poll();
  switch (ev) {
    case Buttons::Event::EmergencyStop:
      emergencyStop = true;
      failsafeActive = false;
      PixelOutput::blackout();
      DMXOutput::blackout();
      OLEDDisplay::showEmergency();
      break;
    case Buttons::Event::CycleMode:
      failsafeActive = !failsafeActive;
      NetworkE131::setManualOverride(failsafeActive);
      break;
    case Buttons::Event::Confirm:
      if (emergencyStop) {
        Buttons::clearEmergency();
        emergencyStop = false;
      }
      break;
    default:
      break;
  }
}

static void handleNetwork() {
  NetworkE131::loop();
  bool active = NetworkE131::isNetworkActive();
  updateStats(active);

  if (emergencyStop) return;

  potValues = PotControl::read();
  FailsafeFX::setSpeed(0.5f + potValues.fxSpeed);

  bool timedOut = millis() - NetworkE131::lastPacketMs() > Config::active.failsafe.timeoutMs;
  if (timedOut) {
    if (!failsafeActive) {
      failsafeActive = true;
      Debug::warn("NET", "Network timeout, enabling failsafe FX");
    }
  } else if (!NetworkE131::manualOverride()) {
    failsafeActive = false;
  }

  float brightnessScalar = potValues.brightness;
  if (failsafeActive || !active) {
    brightnessScalar = std::max(brightnessScalar, Config::active.failsafe.brightnessFloor / 255.0f);
  }

  size_t len = 0;
  const uint8_t *dmx = NetworkE131::dmxData(len);
  if (Config::active.dmx.enabled && !failsafeActive && active) {
    DMXOutput::update(dmx, len);
  }

  if (Config::active.servos.enabled) {
    float servoTargets[4];
    for (size_t i = 0; i < 4; ++i) {
      uint8_t value = (dmx && i < len) ? dmx[i] : 127;
      float norm = value / 255.0f;
      servoTargets[i] = Config::active.servos.minServoAngle +
                        norm * (Config::active.servos.maxServoAngle - Config::active.servos.minServoAngle);
    }
    if (failsafeActive || !active) {
      for (auto &angle : servoTargets) angle = Config::active.servos.neutralAngle;
    }
    JoystickServo::setNetworkTargets(servoTargets, 4);
  }

  size_t pixLen = 0;
  const uint8_t *pixels = NetworkE131::pixelData(pixLen);
  if (Config::active.pixels.enabled) {
    if (failsafeActive || !active) {
      if (Config::active.failsafe.enableFx) {
        PixelOutput::applyFailsafe(brightnessScalar, millis());
      } else {
        PixelOutput::blackout();
      }
    } else {
      PixelOutput::updateFromE131(pixels, pixLen, brightnessScalar);
    }
  }

  JoystickServo::setManualOverride(failsafeActive || NetworkE131::manualOverride());
}

static void handleServos() {
  JoystickServo::update(potValues.brightness, potValues.fxSpeed);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Debug::begin(Debug::Level::Info, true);
  Debug::info("BOOT", "PrizmLink v%s", kFirmwareVersion);

  initLogger();
  initConfig();
  initSubsystems();

  Debug::info("BOOT", "Setup complete");
}

void loop() {
  handleButtons();
  handleNetwork();
  handleServos();

  OLEDDisplay::update(Config::stats, Config::active);
  DMXOutput::loop();
  PixelOutput::loop();
  WebServer::loop(Config::stats);
}

