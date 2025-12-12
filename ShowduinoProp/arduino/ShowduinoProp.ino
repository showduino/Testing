#include <Arduino.h>

// ====== USER CONFIG (edit these in Arduino IDE) ======
// Shared pairing code (must match Brain)
static const uint32_t SHOWDUINO_PAIR_CODE = 12345;

// 1..13 (must match the WiFi/AP channel your Brain is using for ESP-NOW)
static const uint8_t SHOWDUINO_ESPNOW_CHANNEL = 1;

// Prop identity
static const char* SHOWDUINO_PROP_NAME = "LanternA"; // max 16 chars

// Timers
static const uint16_t SHOWDUINO_PROP_ADV_MS = 1500;
static const uint16_t SHOWDUINO_PROP_SENSOR_MS = 200;

// Pins (-1 disables feature)
// ESP32-C3 mini suggestions: LDR=1, BTN=5, DF RX=3, DF TX=4
// ESP8266 NodeMCU suggestions: LDR=A0, BTN=14 (D5), DF RX=12 (D6), DF TX=13 (D7)
static const int SHOWDUINO_LDR_PIN = -1;
static const int SHOWDUINO_BUTTON_PIN = -1;
static const int SHOWDUINO_DFPLAYER_RX = -1; // ESP receives on this pin (connect to DFPlayer TX)
static const int SHOWDUINO_DFPLAYER_TX = -1; // ESP transmits on this pin (connect to DFPlayer RX)

// Optional local LED output (not implemented in this minimal sketch)
static const int SHOWDUINO_LED_PIN = -1;
static const uint16_t SHOWDUINO_LED_COUNT = 0;

// =====================================================

#include <ArduinoJson.h>

// Protocol constants (must match Brain/WLED ShowduinoBridge)
static constexpr uint8_t SDP_MAGIC[4] = {'S','D','P','1'};
static constexpr uint8_t SDP_TYPE_HELLO   = 0x01;
static constexpr uint8_t SDP_TYPE_CMDJSON = 0x02;
static constexpr uint8_t SDP_TYPE_SENSOR  = 0x03;

static unsigned long lastHelloMs = 0;
static unsigned long lastSensorMs = 0;

static uint16_t g_ldr = 0;
static bool g_button = false;
static bool g_mp3Playing = false;
static uint16_t g_mp3Track = 0;
static uint8_t g_mp3Vol = 0;

// DFPlayer (optional)
#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <espnow.h>
  #include <SoftwareSerial.h>
  static uint8_t kBroadcastMac[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  static SoftwareSerial dfSerial(
    (SHOWDUINO_DFPLAYER_RX >= 0) ? SHOWDUINO_DFPLAYER_RX : 12,
    (SHOWDUINO_DFPLAYER_TX >= 0) ? SHOWDUINO_DFPLAYER_TX : 13
  );
#else
  #include <WiFi.h>
  #include <esp_wifi.h>
  #include <esp_now.h>
  static uint8_t kBroadcastMac[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  static HardwareSerial dfSerial(1);
#endif

#include <DFRobotDFPlayerMini.h>
static DFRobotDFPlayerMini dfPlayer;
static bool dfOk = false;

static uint8_t writeHeader(uint8_t* buf, uint8_t type) {
  uint8_t p = 0;
  memcpy(buf + p, SDP_MAGIC, 4); p += 4;
  buf[p++] = (uint8_t)(SHOWDUINO_PAIR_CODE & 0xFF);
  buf[p++] = (uint8_t)((SHOWDUINO_PAIR_CODE >> 8) & 0xFF);
  buf[p++] = (uint8_t)((SHOWDUINO_PAIR_CODE >> 16) & 0xFF);
  buf[p++] = (uint8_t)((SHOWDUINO_PAIR_CODE >> 24) & 0xFF);
  buf[p++] = type;
  return p;
}

static bool espnow_send_raw(const uint8_t* mac, const uint8_t* data, uint8_t len) {
#if defined(ESP8266)
  return esp_now_send((uint8_t*)mac, (uint8_t*)data, len) == 0;
#else
  return esp_now_send(mac, data, len) == ESP_OK;
#endif
}

static void sendHello() {
  String name(SHOWDUINO_PROP_NAME ? SHOWDUINO_PROP_NAME : "prop");
  if (name.length() > 16) name.remove(16);

  uint8_t buf[4 + 4 + 1 + 2 + 1 + 16];
  uint8_t p = writeHeader(buf, SDP_TYPE_HELLO);
  const uint16_t leds = (SHOWDUINO_LED_COUNT > 0) ? SHOWDUINO_LED_COUNT : 0;
  buf[p++] = (uint8_t)(leds & 0xFF);
  buf[p++] = (uint8_t)((leds >> 8) & 0xFF);
  buf[p++] = (uint8_t)name.length();
  memcpy(buf + p, name.c_str(), name.length()); p += (uint8_t)name.length();
  (void)espnow_send_raw(kBroadcastMac, buf, p);
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
  (void)espnow_send_raw(kBroadcastMac, buf, p);
}

static void updateSensors() {
  if (SHOWDUINO_LDR_PIN >= 0) {
    uint16_t raw = (uint16_t)analogRead(SHOWDUINO_LDR_PIN);
    g_ldr = (uint16_t)((g_ldr * 3 + raw) / 4);
  } else {
    g_ldr = 0;
  }

  if (SHOWDUINO_BUTTON_PIN >= 0) {
    static uint8_t stable = 0xFF;
    bool pressed = digitalRead(SHOWDUINO_BUTTON_PIN) == LOW;
    stable = (uint8_t)((stable << 1) | (pressed ? 1 : 0));
    if (stable == 0x00) g_button = false;
    else if (stable == 0xFF) g_button = true;
  } else {
    g_button = false;
  }
}

static void handleCmdJson(const char* json, size_t len) {
  StaticJsonDocument<384> doc;
  if (deserializeJson(doc, json, len)) return;
  JsonObject root = doc.as<JsonObject>();

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
      if (dfOk) dfPlayer.volume((uint8_t)min(vol, 30)); // DFPlayer is 0..30
    }

    if (action && *action) {
      String a(action);
      if (a == "play") {
        if (track > 0) g_mp3Track = (uint16_t)track;
        g_mp3Playing = true;
        if (dfOk && track > 0) dfPlayer.play((uint16_t)track);
      } else if (a == "pause") {
        g_mp3Playing = false;
        if (dfOk) dfPlayer.pause();
      } else if (a == "stop") {
        g_mp3Playing = false;
        if (dfOk) dfPlayer.stop();
      }
    }
  }
}

#if defined(ESP8266)
static void espnow_recv_cb(uint8_t* mac, uint8_t* data, uint8_t len) {
  (void)mac;
  if (len < 10) return;
  if (memcmp(data, SDP_MAGIC, 4) != 0) return;
  uint32_t pair =
    ((uint32_t)data[4]) |
    ((uint32_t)data[5] << 8) |
    ((uint32_t)data[6] << 16) |
    ((uint32_t)data[7] << 24);
  if (SHOWDUINO_PAIR_CODE != 0 && pair != SHOWDUINO_PAIR_CODE) return;
  if (data[8] != SDP_TYPE_CMDJSON) return;
  uint8_t jlen = data[9];
  if (jlen == 0 || (10U + jlen) > len) return;
  handleCmdJson((const char*)(data + 10), jlen);
}
#else
static void espnow_recv_cb(const uint8_t* mac, const uint8_t* data, int len) {
  (void)mac;
  if (len < 10) return;
  if (memcmp(data, SDP_MAGIC, 4) != 0) return;
  uint32_t pair =
    ((uint32_t)data[4]) |
    ((uint32_t)data[5] << 8) |
    ((uint32_t)data[6] << 16) |
    ((uint32_t)data[7] << 24);
  if (SHOWDUINO_PAIR_CODE != 0 && pair != SHOWDUINO_PAIR_CODE) return;
  if (data[8] != SDP_TYPE_CMDJSON) return;
  uint8_t jlen = data[9];
  if (jlen == 0 || (10 + jlen) > len) return;
  handleCmdJson((const char*)(data + 10), jlen);
}
#endif

static bool espnow_init_prop() {
#if defined(ESP8266)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  wifi_set_channel(SHOWDUINO_ESPNOW_CHANNEL);
  if (esp_now_init() != 0) return false;
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_recv_cb(espnow_recv_cb);
  esp_now_add_peer(kBroadcastMac, ESP_NOW_ROLE_COMBO, SHOWDUINO_ESPNOW_CHANNEL, nullptr, 0);
  return true;
#else
  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect(true);
  esp_wifi_set_channel(SHOWDUINO_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) return false;
  esp_now_register_recv_cb(espnow_recv_cb);
  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, kBroadcastMac, 6);
  peer.channel = SHOWDUINO_ESPNOW_CHANNEL;
  peer.encrypt = false;
  (void)esp_now_add_peer(&peer);
  return true;
#endif
}

void setup() {
  Serial.begin(115200);
  delay(50);

  if (SHOWDUINO_BUTTON_PIN >= 0) pinMode(SHOWDUINO_BUTTON_PIN, INPUT_PULLUP);
  if (SHOWDUINO_LDR_PIN >= 0) {
    #if !defined(ESP8266)
      pinMode(SHOWDUINO_LDR_PIN, INPUT);
    #endif
  }

  bool ok = espnow_init_prop();
  Serial.println(ok ? F("ESP-NOW OK") : F("ESP-NOW FAIL"));

  // DFPlayer init (optional)
  if (SHOWDUINO_DFPLAYER_RX >= 0 && SHOWDUINO_DFPLAYER_TX >= 0) {
    #if defined(ESP8266)
      dfSerial.begin(9600);
    #else
      dfSerial.begin(9600, SERIAL_8N1, SHOWDUINO_DFPLAYER_RX, SHOWDUINO_DFPLAYER_TX);
    #endif
    dfOk = dfPlayer.begin(dfSerial);
    Serial.println(dfOk ? F("DFPlayer OK") : F("DFPlayer FAIL"));
    if (dfOk) dfPlayer.volume((uint8_t)min((int)g_mp3Vol, 30));
  }

  lastHelloMs = millis();
  lastSensorMs = millis();
  sendHello();
}

void loop() {
  unsigned long now = millis();

  if ((uint16_t)(now - lastHelloMs) >= SHOWDUINO_PROP_ADV_MS) {
    lastHelloMs = now;
    sendHello();
  }
  if ((uint16_t)(now - lastSensorMs) >= SHOWDUINO_PROP_SENSOR_MS) {
    lastSensorMs = now;
    updateSensors();
    sendSensor();
  }
  yield();
}

