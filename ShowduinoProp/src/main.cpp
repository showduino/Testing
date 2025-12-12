#include <Arduino.h>

#include <ArduinoJson.h>
#include "showduino_protocol.h"

// -------- Build-time config (override in platformio.ini build_flags) --------
#ifndef SHOWDUINO_PAIR_CODE
  #define SHOWDUINO_PAIR_CODE 0
#endif
#ifndef SHOWDUINO_PROP_NAME
  #define SHOWDUINO_PROP_NAME "prop"
#endif
#ifndef SHOWDUINO_PROP_ADV_MS
  #define SHOWDUINO_PROP_ADV_MS 1500
#endif
#ifndef SHOWDUINO_PROP_SENSOR_MS
  #define SHOWDUINO_PROP_SENSOR_MS 200
#endif
#ifndef SHOWDUINO_ESPNOW_CHANNEL
  #define SHOWDUINO_ESPNOW_CHANNEL 1
#endif

// Sensors / IO pins (set to -1 to disable)
#ifndef SHOWDUINO_LDR_PIN
  #define SHOWDUINO_LDR_PIN -1
#endif
#ifndef SHOWDUINO_BUTTON_PIN
  #define SHOWDUINO_BUTTON_PIN -1
#endif

// DFPlayer Mini (set RX/TX pins to enable; both must be >=0)
#ifndef SHOWDUINO_DFPLAYER_RX
  #define SHOWDUINO_DFPLAYER_RX -1
#endif
#ifndef SHOWDUINO_DFPLAYER_TX
  #define SHOWDUINO_DFPLAYER_TX -1
#endif

// Optional local LED output (solid color only)
#ifndef SHOWDUINO_LED_PIN
  #define SHOWDUINO_LED_PIN -1
#endif
#ifndef SHOWDUINO_LED_COUNT
  #define SHOWDUINO_LED_COUNT 0
#endif

// --------------------------------------------------------------------------

static constexpr uint32_t kPairCode = (uint32_t)SHOWDUINO_PAIR_CODE;
static const char* kPropName = SHOWDUINO_PROP_NAME;

static unsigned long lastHelloMs = 0;
static unsigned long lastSensorMs = 0;

// Local state (reported via SENSOR frames)
static uint16_t g_ldr = 0;
static bool g_button = false;
static bool g_mp3Playing = false;
static uint16_t g_mp3Track = 0;
static uint8_t g_mp3Vol = 0;

// Local LED state (if enabled)
static uint8_t g_ledR = 0, g_ledG = 0, g_ledB = 0;
static uint8_t g_ledBri = 255;

// DFPlayer support (optional)
#if (SHOWDUINO_DFPLAYER_RX >= 0) && (SHOWDUINO_DFPLAYER_TX >= 0)
  #include <DFRobotDFPlayerMini.h>
  #if defined(ESP8266)
    #include <SoftwareSerial.h>
    static SoftwareSerial dfSerial(SHOWDUINO_DFPLAYER_RX, SHOWDUINO_DFPLAYER_TX);
  #else
    static HardwareSerial dfSerial(1);
  #endif
  static DFRobotDFPlayerMini dfPlayer;
  static bool dfOk = false;
#endif

// ------------------- ESP-NOW backend (ESP32 + ESP8266) --------------------

#if defined(ESP8266)
  extern "C" {
    #include <user_interface.h>
  }
  #include <espnow.h>
  static uint8_t kBroadcastMac[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

  static void espnow_recv_cb(uint8_t* mac, uint8_t* data, uint8_t len);
  static void espnow_send_cb(uint8_t* mac, uint8_t status) { (void)mac; (void)status; }

  static bool espnow_init_prop() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    wifi_set_channel(SHOWDUINO_ESPNOW_CHANNEL);
    if (esp_now_init() != 0) return false;
    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
    esp_now_register_recv_cb(espnow_recv_cb);
    esp_now_register_send_cb(espnow_send_cb);
    // Add broadcast peer
    esp_now_add_peer(kBroadcastMac, ESP_NOW_ROLE_COMBO, SHOWDUINO_ESPNOW_CHANNEL, nullptr, 0);
    return true;
  }

  static bool espnow_send(const uint8_t* mac, const uint8_t* data, uint8_t len) {
    return esp_now_send((uint8_t*)mac, (uint8_t*)data, len) == 0;
  }

#else
  #include <WiFi.h>
  #include <esp_wifi.h>
  #include <esp_now.h>
  static uint8_t kBroadcastMac[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

  static void espnow_recv_cb(const uint8_t* mac, const uint8_t* data, int len);
  static void espnow_send_cb(const uint8_t* mac, esp_now_send_status_t status) { (void)mac; (void)status; }

  static bool espnow_init_prop() {
    WiFi.mode(WIFI_MODE_STA);
    WiFi.disconnect(true);
    // Keep a stable channel. If you later join a Wi-Fi AP, ESP-NOW channel must match it.
    esp_wifi_set_channel(SHOWDUINO_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (esp_now_init() != ESP_OK) return false;
    esp_now_register_recv_cb(espnow_recv_cb);
    esp_now_register_send_cb(espnow_send_cb);

    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, kBroadcastMac, 6);
    peer.channel = SHOWDUINO_ESPNOW_CHANNEL;
    peer.encrypt = false;
    (void)esp_now_add_peer(&peer);
    return true;
  }

  static bool espnow_send(const uint8_t* mac, const uint8_t* data, uint8_t len) {
    return esp_now_send(mac, data, len) == ESP_OK;
  }
#endif

// --------------------------- Frame builders --------------------------------

static uint8_t writeHeader(uint8_t* buf, uint8_t type) {
  uint8_t p = 0;
  memcpy(buf + p, SDP_MAGIC, 4); p += 4;
  buf[p++] = (uint8_t)(kPairCode & 0xFF);
  buf[p++] = (uint8_t)((kPairCode >> 8) & 0xFF);
  buf[p++] = (uint8_t)((kPairCode >> 16) & 0xFF);
  buf[p++] = (uint8_t)((kPairCode >> 24) & 0xFF);
  buf[p++] = type;
  return p;
}

static void sendHello() {
  const uint16_t leds = (SHOWDUINO_LED_COUNT > 0) ? (uint16_t)SHOWDUINO_LED_COUNT : 0;
  String name(kPropName ? kPropName : "prop");
  if (name.length() > 16) name.remove(16);

  uint8_t buf[4 + 4 + 1 + 2 + 1 + 16];
  uint8_t p = writeHeader(buf, SDP_TYPE_HELLO);
  buf[p++] = (uint8_t)(leds & 0xFF);
  buf[p++] = (uint8_t)((leds >> 8) & 0xFF);
  buf[p++] = (uint8_t)name.length();
  memcpy(buf + p, name.c_str(), name.length()); p += (uint8_t)name.length();

  (void)espnow_send(kBroadcastMac, buf, p);
}

static void sendSensor() {
  uint8_t buf[4 + 4 + 1 + 2 + 1 + 1 + 2 + 1];
  uint8_t p = writeHeader(buf, SDP_TYPE_SENSOR);
  buf[p++] = (uint8_t)(g_ldr & 0xFF);
  buf[p++] = (uint8_t)((g_ldr >> 8) & 0xFF);
  buf[p++] = g_button ? 1 : 0;
  buf[p++] = g_mp3Playing ? 1 : 0;
  buf[p++] = (uint8_t)(g_mp3Track & 0xFF);
  buf[p++] = (uint8_t)((g_mp3Track >> 8) & 0xFF);
  buf[p++] = g_mp3Vol;
  (void)espnow_send(kBroadcastMac, buf, p);
}

// --------------------------- Hardware helpers ------------------------------

static void updateSensors() {
  // LDR
  #if SHOWDUINO_LDR_PIN >= 0
    // simple smoothing
    const uint16_t raw = (uint16_t)analogRead(SHOWDUINO_LDR_PIN);
    g_ldr = (uint16_t)((g_ldr * 3 + raw) / 4);
  #else
    g_ldr = 0;
  #endif

  // Button (active-low with pullup by default)
  #if SHOWDUINO_BUTTON_PIN >= 0
    static uint8_t stable = 0xFF;
    const bool pressed = digitalRead(SHOWDUINO_BUTTON_PIN) == LOW;
    stable = (uint8_t)((stable << 1) | (pressed ? 1 : 0));
    if (stable == 0x00) g_button = false;
    else if (stable == 0xFF) g_button = true;
  #else
    g_button = false;
  #endif
}

static void applyLocalLed() {
  // Placeholder for optional LED output; implement if you wire a strip and want the prop to be self-contained.
  // Kept intentionally minimal; you can swap this for FastLED/NeoPixelBus later.
}

// --------------------------- Command handling ------------------------------

static void handleCmdJson(const char* json, size_t len) {
  StaticJsonDocument<384> doc;
  DeserializationError err = deserializeJson(doc, json, len);
  if (err) return;

  JsonObject root = doc.as<JsonObject>();

  // Minimal MP3 commands (DFPlayer)
  // Accept either:
  // { "cmd":"mp3", "action":"play", "track":3, "volume":20 }
  // OR
  // { "mp3": { "action":"play", ... } }
  JsonObject mp3 = root["mp3"];
  const char* cmd = root["cmd"];
  if (!mp3.isNull() || (cmd && String(cmd) == "mp3")) {
    JsonObject m = mp3.isNull() ? root : mp3;
    const char* action = m["action"] | m["state"] | "";
    int track = m["track"] | m["id"] | -1;
    int vol = m["volume"] | m["vol"] | -1;

    if (vol >= 0) {
      if (vol > 255) vol = 255;
      g_mp3Vol = (uint8_t)vol;
      #if (SHOWDUINO_DFPLAYER_RX >= 0) && (SHOWDUINO_DFPLAYER_TX >= 0)
        if (dfOk) dfPlayer.volume((uint8_t)min(vol, 30)); // DFPlayer is 0..30
      #endif
    }
    if (action && *action) {
      String a(action);
      if (a == "play") {
        if (track > 0) g_mp3Track = (uint16_t)track;
        g_mp3Playing = true;
        #if (SHOWDUINO_DFPLAYER_RX >= 0) && (SHOWDUINO_DFPLAYER_TX >= 0)
          if (dfOk && track > 0) dfPlayer.play((uint16_t)track);
        #endif
      } else if (a == "pause" || a == "stop") {
        g_mp3Playing = false;
        #if (SHOWDUINO_DFPLAYER_RX >= 0) && (SHOWDUINO_DFPLAYER_TX >= 0)
          if (dfOk) {
            if (a == "pause") dfPlayer.pause();
            else dfPlayer.stop();
          }
        #endif
      }
    }
  }

  // Optional: local LED set (solid RGB)
  // { "led": { "r":255,"g":0,"b":0,"bri":128 } }
  JsonObject led = root["led"];
  if (!led.isNull()) {
    g_ledR = led["r"] | g_ledR;
    g_ledG = led["g"] | g_ledG;
    g_ledB = led["b"] | g_ledB;
    g_ledBri = led["bri"] | g_ledBri;
    applyLocalLed();
  }
}

// --------------------------- ESP-NOW RX callback ---------------------------

#if defined(ESP8266)
static void espnow_recv_cb(uint8_t* mac, uint8_t* data, uint8_t len) {
  (void)mac;
  if (len < 9) return;
  if (memcmp(data, SDP_MAGIC, 4) != 0) return;
  const uint32_t pair =
    ((uint32_t)data[4]) |
    ((uint32_t)data[5] << 8) |
    ((uint32_t)data[6] << 16) |
    ((uint32_t)data[7] << 24);
  if (kPairCode != 0 && pair != kPairCode) return;
  const uint8_t type = data[8];
  if (type != SDP_TYPE_CMDJSON) return;
  if (len < 10) return;
  const uint8_t jlen = data[9];
  if (jlen == 0 || (10U + jlen) > len) return;
  handleCmdJson((const char*)(data + 10), jlen);
}
#else
static void espnow_recv_cb(const uint8_t* mac, const uint8_t* data, int len) {
  (void)mac;
  if (len < 9) return;
  if (memcmp(data, SDP_MAGIC, 4) != 0) return;
  const uint32_t pair =
    ((uint32_t)data[4]) |
    ((uint32_t)data[5] << 8) |
    ((uint32_t)data[6] << 16) |
    ((uint32_t)data[7] << 24);
  if (kPairCode != 0 && pair != kPairCode) return;
  const uint8_t type = data[8];
  if (type != SDP_TYPE_CMDJSON) return;
  if (len < 10) return;
  const uint8_t jlen = data[9];
  if (jlen == 0 || (10 + jlen) > len) return;
  handleCmdJson((const char*)(data + 10), jlen);
}
#endif

// --------------------------------- Arduino --------------------------------

void setup() {
  Serial.begin(115200);
  delay(50);

  #if SHOWDUINO_BUTTON_PIN >= 0
    pinMode(SHOWDUINO_BUTTON_PIN, INPUT_PULLUP);
  #endif

  // ADC init is platform-specific; analogRead works after pinMode on ESP32, and A0 on ESP8266.
  #if SHOWDUINO_LDR_PIN >= 0
    #if !defined(ESP8266)
      pinMode(SHOWDUINO_LDR_PIN, INPUT);
    #endif
  #endif

  const bool ok = espnow_init_prop();
  Serial.println(ok ? F("ESP-NOW OK") : F("ESP-NOW FAIL"));

  #if (SHOWDUINO_DFPLAYER_RX >= 0) && (SHOWDUINO_DFPLAYER_TX >= 0)
    #if defined(ESP8266)
      dfSerial.begin(9600);
    #else
      dfSerial.begin(9600, SERIAL_8N1, SHOWDUINO_DFPLAYER_RX, SHOWDUINO_DFPLAYER_TX);
    #endif
    dfOk = dfPlayer.begin(dfSerial);
    if (dfOk) {
      dfPlayer.volume((uint8_t)min((int)g_mp3Vol, 30));
      Serial.println(F("DFPlayer OK"));
    } else {
      Serial.println(F("DFPlayer FAIL"));
    }
  #endif

  lastHelloMs = millis();
  lastSensorMs = millis();
  sendHello();
}

void loop() {
  const unsigned long now = millis();

  if ((uint16_t)(now - lastHelloMs) >= (uint16_t)SHOWDUINO_PROP_ADV_MS) {
    lastHelloMs = now;
    sendHello();
  }

  if ((uint16_t)(now - lastSensorMs) >= (uint16_t)SHOWDUINO_PROP_SENSOR_MS) {
    lastSensorMs = now;
    updateSensors();
    sendSensor();
  }

  yield();
}

