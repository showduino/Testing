/**
 * Showduino Brain Firmware – ESP32-S3 (Core Controller)
 *
 * Handles ESP-NOW commands from the UI, drives attached hardware,
 * monitors environmental add-ons, and reports status back to the UI.
 *
 * Features implemented:
 *  - Core relay / LED / MP3 control stubs
 *  - Add-on framework matching the UI registry (Smoke, NeoPixel, Relay Expander, MP3, R3 FX)
 *  - DHT11 temperature sensing
 *  - PWM MOSFET fan control with automatic temperature trigger
 *  - DS3231 RTC integration for time-keeping (shared with UI)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <Wire.h>
#include <RTClib.h>
#include <DHT.h>
#include <math.h>

// Optional peripheral libraries
#include <Adafruit_NeoPixel.h>

// ────────────────────────────────────────────────
//  Hardware Configuration
// ────────────────────────────────────────────────
#define RELAY_COUNT           8
static const uint8_t RELAY_PINS[RELAY_COUNT] = {4, 5, 6, 7, 8, 9, 10, 11};

static constexpr uint8_t LED_LINE_COUNT = 4;
static const uint8_t LED_PIXEL_PINS[LED_LINE_COUNT] = {38, 39, 40, 41};
static constexpr uint16_t LED_PIXELS_PER_LINE = 120;

#define SMOKE_RELAY_PIN       12
#define FAN_PWM_PIN           14
#define FAN_PWM_CHANNEL       2
#define FAN_PWM_FREQ          25000
#define FAN_PWM_RESOLUTION    8

#define DHT_PIN               3
#define DHT_TYPE              DHT11

#define STATUS_INTERVAL_MS    250
#define DHT_INTERVAL_MS       2000
#define FAN_TEMP_THRESHOLD_C  38.0f
#define FAN_FULL_TEMP_C       48.0f

// ────────────────────────────────────────────────
//  Command / Status Structures (mirrors UI)
// ────────────────────────────────────────────────
enum CommandType : uint8_t {
  CMD_NOP = 0,
  CMD_RELAY = 1,
  CMD_CONTROL_MODE = 2,
  CMD_TIMELINE_PREVIEW = 3,
  CMD_TIMELINE_PLAY = 4,
  CMD_MP3_PLAY = 5,
  CMD_MP3_STOP = 6,
  CMD_MP3_VOLUME = 7,
  CMD_LED = 8,
  CMD_STOP_ALL = 9,
  CMD_NEXT_CUE = 10,
  CMD_PREV_CUE = 11,
  CMD_PING = 12,
  CMD_TIMELINE_REWIND = 13,
  CMD_TIMELINE_FWD = 14,
  CMD_TIMELINE_LOOP = 15,
  CMD_SYNC_SD = 16,
  CMD_MP3_SCAN = 17,
  CMD_REQUEST_FILE_LIST = 18,
  CMD_ADDON_ACTION = 19
};

enum ControlMode : uint8_t {
  MODE_AUTO = 0,
  MODE_MANUAL = 1
};

enum AddonAction : uint8_t {
  ADDON_SMOKE_TRIGGER = 1,
  ADDON_SMOKE_INTENSITY = 2,
  ADDON_SMOKE_PURGE = 3,
  ADDON_PIXELS_BRIGHTNESS = 10,
  ADDON_PIXELS_EFFECT = 11,
  ADDON_RELAY_TOGGLE_BASE = 20,
  ADDON_MP3_PLAY = 30,
  ADDON_MP3_STOP = 31,
  ADDON_MP3_VOLUME_A = 32,
  ADDON_MP3_VOLUME_B = 33,
  ADDON_MP3_SYNC = 34,
  ADDON_R3_PRESET = 40
};

struct __attribute__((packed)) ShowduinoCommand {
  uint8_t type;
  uint8_t id;
  uint16_t value;
  uint32_t position;
};

struct __attribute__((packed)) ShowduinoStatus {
  uint8_t mode;
  uint8_t relays[8];
  uint8_t mp3Vol[2];
  uint8_t mp3State[2];
  uint16_t ledBrightness[4];
  uint32_t timelinePos;
  uint8_t activeCue;
};

static_assert(sizeof(ShowduinoCommand) == 8, "Command struct mismatch");
static_assert(sizeof(ShowduinoStatus) == 26, "Status struct mismatch");

// ────────────────────────────────────────────────
//  State
// ────────────────────────────────────────────────
static ShowduinoStatus status = {
  MODE_AUTO,
  {0},
  {128, 128},
  {0, 0},
  {0, 0, 0, 0},
  0,
  0
};

static bool timelineLoopEnabled = false;
static uint32_t lastStatusSend = 0;
static uint32_t lastDhtSample = 0;
static float lastTemperature = 0.0f;

static RTC_DS3231 rtc;
static bool rtcReady = false;

static DHT dht(DHT_PIN, DHT_TYPE);
static Adafruit_NeoPixel pixelLines[LED_LINE_COUNT] = {
  Adafruit_NeoPixel(LED_PIXELS_PER_LINE, LED_PIXEL_PINS[0], NEO_GRB + NEO_KHZ800),
  Adafruit_NeoPixel(LED_PIXELS_PER_LINE, LED_PIXEL_PINS[1], NEO_GRB + NEO_KHZ800),
  Adafruit_NeoPixel(LED_PIXELS_PER_LINE, LED_PIXEL_PINS[2], NEO_GRB + NEO_KHZ800),
  Adafruit_NeoPixel(LED_PIXELS_PER_LINE, LED_PIXEL_PINS[3], NEO_GRB + NEO_KHZ800),
};

static uint8_t uiPeer[6] = {0};
static bool peerKnown = false;

static bool brainI2cPresent[128] = {false};

struct BrainAddonDef {
  uint8_t addonId;
  bool requiresI2C;
  uint8_t i2cAddr;
};

static const BrainAddonDef brainAddonDefs[] = {
  {10, false, 0x00}, // Smoke relay (direct drive)
  {11, false, 0x00}, // NeoPixel (GPIO driven)
  {12, true,  0x3E}, // Relay expander (e.g., SX1509)
  {13, false, 0x00}, // MP3 players (UART/I2C handled elsewhere)
  {14, true,  0x41}  // R3 Terminal (hypothetical I2C FX board)
};
static constexpr size_t brainAddonCount = sizeof(brainAddonDefs) / sizeof(BrainAddonDef);
static bool brainAddonAvailable[brainAddonCount] = {false};

static constexpr uint8_t MAX_PIXEL_GROUPS = 8;
struct PixelGroupState {
  bool active;
  uint8_t line;
  uint8_t start;
  uint8_t length;
  uint8_t effect;
  uint8_t brightness;
};

static PixelGroupState pixelGroups[MAX_PIXEL_GROUPS] = {};
static uint32_t lastPixelFrameMs = 0;

// ────────────────────────────────────────────────
//  Helper Functions
// ────────────────────────────────────────────────
void setRelay(uint8_t index, bool on) {
  if (index >= RELAY_COUNT) return;
  digitalWrite(RELAY_PINS[index], on ? HIGH : LOW);
  status.relays[index] = on ? 1 : 0;
}

void setFanDuty(float temperatureC) {
  float duty = 0.0f;
  if (temperatureC >= FAN_TEMP_THRESHOLD_C) {
    duty = (temperatureC - FAN_TEMP_THRESHOLD_C) /
           (FAN_FULL_TEMP_C - FAN_TEMP_THRESHOLD_C);
    duty = constrain(duty, 0.2f, 1.0f);  // minimum 20% once active
  }
  uint32_t pwmValue = duty * ((1 << FAN_PWM_RESOLUTION) - 1);
  ledcWrite(FAN_PWM_CHANNEL, pwmValue);
}

const BrainAddonDef* findBrainAddon(uint8_t addonId, size_t *indexOut = nullptr) {
  for (size_t i = 0; i < brainAddonCount; ++i) {
    if (brainAddonDefs[i].addonId == addonId) {
      if (indexOut) *indexOut = i;
      return &brainAddonDefs[i];
    }
  }
  return nullptr;
}

PixelGroupState *getPixelGroup(uint8_t line, uint8_t start, uint8_t length, size_t *indexOut = nullptr) {
  if (length == 0) length = 1;
  PixelGroupState *inactiveSlot = nullptr;
  size_t inactiveIndex = MAX_PIXEL_GROUPS;
  for (size_t i = 0; i < MAX_PIXEL_GROUPS; ++i) {
    PixelGroupState &group = pixelGroups[i];
    if (group.active && group.line == line && group.start == start && group.length == length) {
      if (indexOut) *indexOut = i;
      return &group;
    }
    if (group.active && group.line == line && group.start == start) {
      group.length = length;
      if (indexOut) *indexOut = i;
      return &group;
    }
    if (!group.active && !inactiveSlot) {
      inactiveSlot = &group;
      inactiveIndex = i;
    }
  }
  if (!inactiveSlot) {
    inactiveSlot = &pixelGroups[MAX_PIXEL_GROUPS - 1];
    inactiveIndex = MAX_PIXEL_GROUPS - 1;
  }
  inactiveSlot->active = true;
  inactiveSlot->line = line % LED_LINE_COUNT;
  inactiveSlot->start = start;
  inactiveSlot->length = length;
  inactiveSlot->effect = 0;
  inactiveSlot->brightness = inactiveSlot->brightness == 0 ? 180 : inactiveSlot->brightness;
  if (indexOut) *indexOut = inactiveIndex;
  return inactiveSlot;
}

static inline uint8_t scaleBrightness(uint8_t base, uint8_t factor) {
  return (uint16_t)base * factor / 255;
}

uint32_t computeEffectColor(const PixelGroupState &group, uint8_t offset, uint32_t nowMs) {
  uint8_t level = group.brightness;
  const Adafruit_NeoPixel &strip = pixelLines[group.line % LED_LINE_COUNT];
  switch (group.effect) {
    case 0: // Solid
      return strip.Color(level, level, level);
    case 1: { // Flash
      bool on = ((nowMs / 200) & 0x01) == 0;
      uint8_t v = on ? level : level / 20;
      return strip.Color(v, v, v);
    }
    case 2: { // Chase
      uint16_t span = group.length == 0 ? 1 : group.length;
      uint16_t pos = (nowMs / 60) % span;
      uint8_t intensity = (offset == pos) ? level : level / 16;
      return strip.Color(intensity, 0, intensity);
    }
    case 3: { // Twinkle
      uint8_t noise = (uint8_t)(((nowMs / 5) + offset * 73) & 0xFF);
      bool burst = noise > 240;
      if (burst) {
        return strip.Color(level, level, level);
      }
      uint8_t sparkle = scaleBrightness(level, noise);
      return strip.Color(sparkle / 2, sparkle, sparkle / 3);
    }
    case 4: { // Gradient
      float pos = (group.length > 1) ? (float)offset / (group.length - 1) : 0.0f;
      uint8_t r = (uint8_t)(level * (1.0f - pos));
      uint8_t g = (uint8_t)(level * pos);
      uint8_t b = scaleBrightness(level, 180);
      return strip.Color(r, g, b);
    }
    case 5: { // Wave
      float phase = (nowMs * 0.005f) + (offset * 0.3f);
      float intensity = (sinf(phase) + 1.0f) * 0.5f;
      uint8_t v = (uint8_t)(level * intensity);
      return strip.Color(0, v, level);
    }
    default:
      return strip.Color(level, level, level);
  }
}

void renderPixelGroups(bool force = false) {
  uint32_t now = millis();
  if (!force && (now - lastPixelFrameMs) < 33) {
    return;
  }
  lastPixelFrameMs = now;
  for (uint8_t line = 0; line < LED_LINE_COUNT; ++line) {
    pixelLines[line].clear();
    status.ledBrightness[line] = 0;
  }
  for (size_t i = 0; i < MAX_PIXEL_GROUPS; ++i) {
    PixelGroupState &group = pixelGroups[i];
    if (!group.active) continue;
    uint8_t line = group.line % LED_LINE_COUNT;
    Adafruit_NeoPixel &strip = pixelLines[line];
    if (group.start >= LED_PIXELS_PER_LINE) continue;
    if (group.length == 0) group.length = 1;
    uint16_t cappedLength = group.length;
    if (group.start + cappedLength > LED_PIXELS_PER_LINE) {
      cappedLength = LED_PIXELS_PER_LINE - group.start;
    }
    if (group.brightness > status.ledBrightness[line]) {
      status.ledBrightness[line] = group.brightness;
    }
    for (uint16_t offset = 0; offset < cappedLength; ++offset) {
      uint32_t color = computeEffectColor(group, offset, now);
      strip.setPixelColor(group.start + offset, color);
    }
    yield();
  }
  for (uint8_t line = 0; line < LED_LINE_COUNT; ++line) {
    pixelLines[line].show();
  }
  yield();
}

bool brainAddonIsAvailable(uint8_t addonId) {
  size_t idx = 0;
  const BrainAddonDef *def = findBrainAddon(addonId, &idx);
  if (!def) {
    // Unknown add-on: treat as optional (ignore commands gracefully).
    return false;
  }
  if (!def->requiresI2C) {
    return true;
  }
  return brainAddonAvailable[idx];
}

void scanI2CDevices() {
  memset(brainI2cPresent, 0, sizeof(brainI2cPresent));
  Serial.println("[I2C] Scanning bus...");
  for (uint8_t addr = 1; addr < 0x7F; ++addr) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      brainI2cPresent[addr] = true;
      Serial.printf("[I2C] ✓ 0x%02X detected\n", addr);
    }
    yield();
  }
}

void updateBrainAddonAvailability() {
  for (size_t i = 0; i < brainAddonCount; ++i) {
    const BrainAddonDef &def = brainAddonDefs[i];
    if (!def.requiresI2C) {
      brainAddonAvailable[i] = true;
    } else {
      uint8_t addr = def.i2cAddr & 0x7F;
      brainAddonAvailable[i] = brainI2cPresent[addr];
    }
    Serial.printf("[ADDON] %u -> %s\n",
                  def.addonId,
                  brainAddonAvailable[i] ? "available" : "missing");
    yield();
  }
}

void handleSmokeAction(uint8_t action, uint16_t param) {
  switch (action) {
    case ADDON_SMOKE_TRIGGER:
      digitalWrite(SMOKE_RELAY_PIN, HIGH);
      delay(param > 0 ? param : 2000);
      digitalWrite(SMOKE_RELAY_PIN, LOW);
      break;
    case ADDON_SMOKE_INTENSITY:
      // For demo purposes map intensity to simple PWM fan assist
      ledcWrite(FAN_PWM_CHANNEL, map(param, 0, 255, 0, 255));
      break;
    case ADDON_SMOKE_PURGE:
      digitalWrite(SMOKE_RELAY_PIN, LOW);
      break;
    default:
      break;
  }
}

void handlePixelAction(uint8_t action, uint16_t param, uint16_t aux) {
  uint8_t line = (aux >> 14) & 0x03;
  uint8_t start = (aux >> 7) & 0x7F;
  uint8_t length = aux & 0x7F;
  if (length == 0) length = LED_PIXELS_PER_LINE;
  if (start >= LED_PIXELS_PER_LINE) start = LED_PIXELS_PER_LINE - 1;
  if (length > (LED_PIXELS_PER_LINE - start)) {
    length = LED_PIXELS_PER_LINE - start;
  }

  size_t groupIndex = 0;
  PixelGroupState *group = getPixelGroup(line, start, length, &groupIndex);
  if (!group) return;
  group->line = line % LED_LINE_COUNT;

  if (action == ADDON_PIXELS_BRIGHTNESS) {
    group->brightness = constrain(param, 0u, 255u);
    status.ledBrightness[group->line] = group->brightness;
  } else if (action == ADDON_PIXELS_EFFECT) {
    group->effect = param;
    if (group->brightness == 0) {
      group->brightness = 255;
    }
    status.ledBrightness[group->line] = group->brightness;
  }
  renderPixelGroups(true);
  yield();
}

void handleRelayExpanderAction(uint8_t action) {
  if (action >= ADDON_RELAY_TOGGLE_BASE) {
    uint8_t relayIndex = action - ADDON_RELAY_TOGGLE_BASE;
    setRelay(relayIndex, true);
    delay(100);
    setRelay(relayIndex, false);
  }
}

void handleMp3AddonAction(uint8_t action, uint16_t param) {
  switch (action) {
    case ADDON_MP3_PLAY:
      status.mp3State[param & 0x01] = 1;
      break;
    case ADDON_MP3_STOP:
      status.mp3State[param & 0x01] = 0;
      break;
    case ADDON_MP3_VOLUME_A:
      status.mp3Vol[0] = constrain(param, 0u, 255u);
      break;
    case ADDON_MP3_VOLUME_B:
      status.mp3Vol[1] = constrain(param, 0u, 255u);
      break;
    case ADDON_MP3_SYNC:
      // Placeholder for sync routine
      break;
    default:
      break;
  }
}

void handleR3AddonAction(uint16_t preset) {
  // Placeholder: map presets to simple LED changes
  Serial.printf("[R3 FX] Trigger preset %u\n", preset);
}

void handleAddonCommand(uint8_t addonId, uint32_t packedAction, uint16_t value) {
  uint8_t action = (packedAction >> 16) & 0xFF;
  uint16_t aux = packedAction & 0xFFFF;
  if (!brainAddonIsAvailable(addonId)) {
    Serial.printf("[ADDON] Ignored command for add-on %u (not available)\n", addonId);
    return;
  }
  switch (addonId) {
    case 10: handleSmokeAction(action, value); break;
    case 11: handlePixelAction(action, value, aux); break;
    case 12: handleRelayExpanderAction(action); break;
    case 13: handleMp3AddonAction(action, value); break;
    case 14: handleR3AddonAction(value); break;
    default: break;
  }
}

void sendStatus() {
  uint8_t buffer[sizeof(ShowduinoStatus)];
  memcpy(buffer, &status, sizeof(status));
  if (peerKnown) {
    esp_now_send(uiPeer, buffer, sizeof(buffer));
  } else {
    // broadcast status if no peer known
    uint8_t broadcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    esp_now_send(broadcast, buffer, sizeof(buffer));
  }
}

void onDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
  if (len != sizeof(ShowduinoCommand)) return;
  ShowduinoCommand cmd;
  memcpy(&cmd, data, sizeof(cmd));

  if (!peerKnown) {
    memcpy(uiPeer, mac, sizeof(uiPeer));
    peerKnown = true;
    Serial.printf("[ESP-NOW] Paired with UI %02X:%02X:%02X:%02X:%02X:%02X\n",
                  uiPeer[0], uiPeer[1], uiPeer[2], uiPeer[3], uiPeer[4], uiPeer[5]);
  }

  switch (cmd.type) {
    case CMD_RELAY:
      setRelay(cmd.id, cmd.value);
      break;
    case CMD_CONTROL_MODE:
      status.mode = cmd.value ? MODE_MANUAL : MODE_AUTO;
      break;
    case CMD_LED:
      {
        uint8_t line = cmd.id % LED_LINE_COUNT;
        status.ledBrightness[line] = cmd.value;
        pixelLines[line].setBrightness(cmd.value);
      }
      renderPixelGroups(true);
      break;
    case CMD_MP3_PLAY:
      status.mp3State[cmd.id & 0x01] = 1;
      break;
    case CMD_MP3_STOP:
      status.mp3State[cmd.id & 0x01] = 0;
      break;
    case CMD_MP3_VOLUME:
      status.mp3Vol[cmd.id & 0x01] = cmd.value;
      break;
    case CMD_STOP_ALL:
      for (uint8_t i = 0; i < RELAY_COUNT; ++i) setRelay(i, false);
      status.mp3State[0] = status.mp3State[1] = 0;
      break;
    case CMD_TIMELINE_PLAY:
      status.timelinePos = cmd.position;
      status.activeCue++;
      break;
    case CMD_TIMELINE_PREVIEW:
      status.timelinePos = cmd.position;
      break;
    case CMD_TIMELINE_LOOP:
      timelineLoopEnabled = (cmd.value != 0);
      break;
    case CMD_PING:
      break;
    case CMD_ADDON_ACTION:
      handleAddonCommand(cmd.id, cmd.position, cmd.value);
      break;
    default:
      break;
  }
}

void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {
  Serial.printf("[ESP-NOW] Send status: %s\n", status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

void sampleTemperature() {
  uint32_t now = millis();
  if (now - lastDhtSample < DHT_INTERVAL_MS) return;
  lastDhtSample = now;
  float t = dht.readTemperature();
  if (!isnan(t)) {
    lastTemperature = t;
    setFanDuty(t);
  }
}

void loopStatus() {
  uint32_t now = millis();
  if (now - lastStatusSend >= STATUS_INTERVAL_MS) {
    lastStatusSend = now;
    if (rtcReady) {
      DateTime nowRtc = rtc.now();
      status.timelinePos = nowRtc.secondstime();
    }
    sendStatus();
  }
}

void configureRelays() {
  for (uint8_t i = 0; i < RELAY_COUNT; ++i) {
    pinMode(RELAY_PINS[i], OUTPUT);
    digitalWrite(RELAY_PINS[i], LOW);
  }
  pinMode(SMOKE_RELAY_PIN, OUTPUT);
  digitalWrite(SMOKE_RELAY_PIN, LOW);
}

void setupFan() {
  ledcSetup(FAN_PWM_CHANNEL, FAN_PWM_FREQ, FAN_PWM_RESOLUTION);
  ledcAttachPin(FAN_PWM_PIN, FAN_PWM_CHANNEL);
  setFanDuty(0.0f);
}

void initRTC() {
  rtcReady = rtc.begin();
  if (!rtcReady) {
    Serial.println("[RTC] ⚠️ DS3231 not detected");
    return;
  }
  if (rtc.lostPower()) {
    Serial.println("[RTC] ⚠️ Lost power, setting to compile time");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  Serial.println("[RTC] ✓ DS3231 initialised");
}

void initESPNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] ❌ Init failed");
    while (true) delay(1000);
  }
  esp_now_register_recv_cb(onDataRecv);
  esp_now_register_send_cb(onDataSent);

  esp_now_peer_info_t broadcastPeer = {};
  memset(broadcastPeer.peer_addr, 0xFF, 6);
  broadcastPeer.channel = 0;
  broadcastPeer.encrypt = false;
  esp_now_add_peer(&broadcastPeer);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("╔═════════════════════════════════════════════════════╗");
  Serial.println("║  SHOWDUINO BRAIN – ESP-NOW CORE CONTROLLER          ║");
  Serial.println("╚═════════════════════════════════════════════════════╝");

  configureRelays();
  setupFan();

  Wire.begin();
  dht.begin();
  for (uint8_t line = 0; line < LED_LINE_COUNT; ++line) {
    pixelLines[line].begin();
    pixelLines[line].setBrightness(255);
    pixelLines[line].clear();
    pixelLines[line].show();
  }
  renderPixelGroups(true);

  initRTC();
  initESPNow();

  scanI2CDevices();
  updateBrainAddonAvailability();

  Serial.println("[BOOT] Brain ready.");
}

void loop() {
  sampleTemperature();
  renderPixelGroups(false);
  loopStatus();
  delay(5);
}
