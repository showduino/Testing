#include <Wire.h>
#include "joystick_servo.h"
#include "debug_utils.h"
#include <algorithm>

namespace JoystickServo {

static Adafruit_PWMServoDriver sDriver;
static bool sReady = false;
static Prizm::ServoConfig sCfg;
static ServoState sState;
static bool sBtn1Last = false;
static bool sBtn2Last = false;
static bool sNetworkManual = false;

static uint16_t angleToPulse(float angle) {
  angle = constrain(angle, sCfg.minServoAngle, sCfg.maxServoAngle);
  float span = sCfg.maxServoAngle - sCfg.minServoAngle;
  float norm = (angle - sCfg.minServoAngle) / span;
  float micro = 500.0f + norm * 2000.0f; // 500-2500us
  return static_cast<uint16_t>((micro * 4096.0f) / 20000.0f);
}

static float readAnalog(uint8_t pin) {
  if (pin == UINT8_MAX) return 0.0f;
  int raw = analogRead(pin);
  return raw / 4095.0f;
}

bool begin(const Prizm::ServoConfig &cfg) {
  if (!cfg.enabled) {
    Debug::warn("Servo", "PCA9685 disabled via config");
    return false;
  }

  sCfg = cfg;
  Wire.begin(cfg.sda, cfg.scl, 400000);
  sDriver = Adafruit_PWMServoDriver(cfg.pcaAddress);
  if (!sDriver.begin()) {
    Debug::error("Servo", "PCA9685 not found (0x%02X)", cfg.pcaAddress);
    return false;
  }
  sDriver.setPWMFreq(50);
  for (int i = 0; i < 4; ++i) {
    sState.current[i] = cfg.neutralAngle;
    sState.target[i] = cfg.neutralAngle;
    sDriver.setPWM(i, 0, angleToPulse(cfg.neutralAngle));
  }
  pinMode(cfg.button1Pin, cfg.button1ActiveState == LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
  pinMode(cfg.button2Pin, cfg.button2ActiveState == LOW ? INPUT_PULLUP : INPUT_PULLDOWN);

  sReady = true;
  Debug::info("Servo", "PCA9685 initialized");
  return true;
}

void setNetworkTargets(const float *angles, size_t count) {
  if (!sReady || !angles) return;
  size_t limit = std::min<size_t>(count, 4);
  for (size_t i = 0; i < limit; ++i) {
    sState.target[i] = angles[i];
  }
}

void setManualOverride(bool enabled) {
  sNetworkManual = enabled;
}

void update(float brightnessScalar, float speedScalar) {
  if (!sReady) return;

  bool button1 = digitalRead(sCfg.button1Pin) == sCfg.button1ActiveState;
  bool button2 = digitalRead(sCfg.button2Pin) == sCfg.button2ActiveState;

  if (button1 && !sBtn1Last) {
    for (auto &t : sState.target) t = sCfg.neutralAngle;
    sState.manual = true;
  }
  if (button2 && !sBtn2Last) {
    sState.manual = !sState.manual;
  }
  sBtn1Last = button1;
  sBtn2Last = button2;

  float joy1x = readAnalog(sCfg.joystickXPin) * 2.0f - 1.0f;
  float joy1y = readAnalog(sCfg.joystickYPin) * 2.0f - 1.0f;
  float joy2x = readAnalog(sCfg.joystick2XPin) * 2.0f - 1.0f;
  float joy2y = readAnalog(sCfg.joystick2YPin) * 2.0f - 1.0f;

  bool manualActive = sState.manual || sNetworkManual;

  if (manualActive) {
    sState.target[0] = constrain(sCfg.neutralAngle + joy1x * 60.0f, sCfg.minServoAngle, sCfg.maxServoAngle);
    sState.target[1] = constrain(sCfg.neutralAngle + joy1y * 60.0f, sCfg.minServoAngle, sCfg.maxServoAngle);
    sState.target[2] = constrain(sCfg.neutralAngle + joy2x * 60.0f, sCfg.minServoAngle, sCfg.maxServoAngle);
    sState.target[3] = constrain(sCfg.neutralAngle + joy2y * 60.0f, sCfg.minServoAngle, sCfg.maxServoAngle);
  }

  float smoothing = 0.15f + (1.0f - brightnessScalar) * 0.35f;
  smoothing /= std::max(speedScalar, 0.1f);
  smoothing = constrain(smoothing, 0.05f, 0.6f);

  for (int i = 0; i < 4; ++i) {
    float delta = sState.target[i] - sState.current[i];
    sState.current[i] += delta * smoothing;
    sDriver.setPWM(i, 0, angleToPulse(sState.current[i]));
  }

  sState.manual = manualActive;
}

ServoState state() {
  return sState;
}

} // namespace JoystickServo

