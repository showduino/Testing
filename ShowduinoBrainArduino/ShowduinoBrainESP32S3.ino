#include <Arduino.h>

// ===================== USER CONFIG (Arduino IDE) =====================
// Pairing code (must match props). 0 disables filtering (not recommended).
static const uint32_t PAIR_CODE = 12345;

// ESP-NOW channel (must match props). If your props are locked to channel 1, set 1 here.
static const uint8_t ESPNOW_CHANNEL = 1;

// Prop offline threshold
static const uint32_t PROP_TIMEOUT_MS = 5000;

// Optional Wi-Fi (for controlling stock WLED devices via /json/state).
// Set SSID to "" to disable Wi-Fi entirely.
static const char* WIFI_SSID = "";
static const char* WIFI_PASS = "";

// =====================================================================

#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <HTTPClient.h>

#include <ArduinoJson.h>
#include "showduino_protocol.h"

// ---------------------------- Prop registry ---------------------------
struct PropEntry {
  uint8_t mac[6];
  char name[17];
  uint16_t leds;

  uint16_t ldr;
  bool button;
  bool mp3Playing;
  uint16_t mp3Track;
  uint8_t mp3Vol;

  uint32_t lastSeen;
  bool present;
};

static constexpr uint8_t MAX_PROPS = 12;
static PropEntry props[MAX_PROPS]{};

static bool macEquals(const uint8_t* a, const uint8_t* b) {
  for (int i = 0; i < 6; i++) if (a[i] != b[i]) return false;
  return true;
}

static void macTo12(const uint8_t* mac, char out[13]) {
  sprintf(out, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  out[12] = 0;
}

static int findPropByMac(const uint8_t* mac) {
  for (uint8_t i = 0; i < MAX_PROPS; i++) {
    if (props[i].present && macEquals(props[i].mac, mac)) return (int)i;
  }
  return -1;
}

static int findPropByName(const char* name) {
  if (!name || !*name) return -1;
  for (uint8_t i = 0; i < MAX_PROPS; i++) {
    if (!props[i].present) continue;
    if (strncmp(props[i].name, name, 16) == 0) return (int)i;
  }
  return -1;
}

static int allocProp(const uint8_t* mac) {
  int idx = findPropByMac(mac);
  if (idx >= 0) return idx;
  for (uint8_t i = 0; i < MAX_PROPS; i++) {
    if (!props[i].present) {
      props[i].present = true;
      memcpy(props[i].mac, mac, 6);
      props[i].name[0] = 0;
      props[i].leds = 0;
      props[i].ldr = 0;
      props[i].button = false;
      props[i].mp3Playing = false;
      props[i].mp3Track = 0;
      props[i].mp3Vol = 0;
      props[i].lastSeen = millis();
      return (int)i;
    }
  }
  return -1;
}

static bool isOnline(const PropEntry& p) {
  if (!p.present) return false;
  return (millis() - p.lastSeen) <= PROP_TIMEOUT_MS;
}

// ------------------------- ESP-NOW send helpers ------------------------

static bool ensurePeer(const uint8_t mac[6]) {
  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = ESPNOW_CHANNEL;
  peer.encrypt = false;
  if (esp_now_is_peer_exist(mac)) return true;
  return esp_now_add_peer(&peer) == ESP_OK;
}

static bool sendEspNowCmdJsonToMac(const uint8_t mac[6], const char* json, size_t len) {
  if (!json || !len || len > 200) return false;
  if (!ensurePeer(mac)) return false;

  uint8_t buf[4 + 4 + 1 + 1 + 200];
  uint8_t p = 0;
  memcpy(buf + p, SDP_MAGIC, 4); p += 4;
  buf[p++] = (uint8_t)(PAIR_CODE & 0xFF);
  buf[p++] = (uint8_t)((PAIR_CODE >> 8) & 0xFF);
  buf[p++] = (uint8_t)((PAIR_CODE >> 16) & 0xFF);
  buf[p++] = (uint8_t)((PAIR_CODE >> 24) & 0xFF);
  buf[p++] = SDP_TYPE_CMDJSON;
  buf[p++] = (uint8_t)len;
  memcpy(buf + p, json, len); p += (uint8_t)len;

  return esp_now_send(mac, buf, p) == ESP_OK;
}

// --------------------------- WLED HTTP helper ---------------------------
static bool postWledJsonState(const char* hostOrIp, JsonObject state) {
  if (!hostOrIp || !*hostOrIp) return false;
  if (WiFi.status() != WL_CONNECTED) return false;

  String url = String("http://") + hostOrIp + "/json/state";
  String body;
  serializeJson(state, body);

  HTTPClient http;
  http.setTimeout(400);
  if (!http.begin(url)) return false;
  http.addHeader("Content-Type", "application/json");
  int code = http.POST((uint8_t*)body.c_str(), body.length());
  http.end();
  return code > 0 && code < 400;
}

// --------------------------- ESP-NOW RX callback -------------------------
static void onEspNowRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (!info || !data || len < 9) return;
  if (memcmp(data, SDP_MAGIC, 4) != 0) return;

  const uint32_t pair =
    ((uint32_t)data[4]) |
    ((uint32_t)data[5] << 8) |
    ((uint32_t)data[6] << 16) |
    ((uint32_t)data[7] << 24);
  if (PAIR_CODE != 0 && pair != PAIR_CODE) return;

  const uint8_t type = data[8];
  const uint8_t* mac = info->src_addr;
  int idx = allocProp(mac);
  if (idx < 0) return;

  props[idx].lastSeen = millis();

  if (type == SDP_TYPE_HELLO) {
    if (len < 12) return;
    props[idx].leds = (uint16_t)data[9] | ((uint16_t)data[10] << 8);
    uint8_t nlen = data[11];
    if (nlen > 16) nlen = 16;
    if (12 + nlen > (uint32_t)len) nlen = (len > 12) ? (uint8_t)(len - 12) : 0;
    memset(props[idx].name, 0, sizeof(props[idx].name));
    if (nlen) memcpy(props[idx].name, data + 12, nlen);
    return;
  }

  if (type == SDP_TYPE_SENSOR) {
    if (len < (4 + 4 + 1 + 2 + 1 + 1 + 2 + 1)) return;
    int p = 9;
    props[idx].ldr = (uint16_t)data[p] | ((uint16_t)data[p + 1] << 8); p += 2;
    props[idx].button = data[p++] != 0;
    props[idx].mp3Playing = data[p++] != 0;
    props[idx].mp3Track = (uint16_t)data[p] | ((uint16_t)data[p + 1] << 8); p += 2;
    props[idx].mp3Vol = data[p++];
    return;
  }
}

// --------------------------- Serial command API ---------------------------
// Send one JSON object per line.
//
// 1) List props:
// {"cmd":"list"}
//
// 2) Send to prop by NAME or MAC12:
// {"cmd":"prop","targets":["LanternA"],"payload":{"cmd":"mp3","action":"play","track":3,"volume":20}}
//
// 3) Send to a WLED device over Wi-Fi:
// {"cmd":"wled","host":"192.168.1.50","state":{"on":true,"bri":128,"ps":1}}
//
static String lineBuf;

static void printPropsJson() {
  StaticJsonDocument<1024> doc;
  JsonArray arr = doc.createNestedArray("props");
  for (uint8_t i = 0; i < MAX_PROPS; i++) {
    if (!props[i].present) continue;
    JsonObject o = arr.createNestedObject();
    char mac12[13];
    macTo12(props[i].mac, mac12);
    o["mac"] = mac12;
    o["name"] = props[i].name;
    o["leds"] = props[i].leds;
    uint32_t age = millis() - props[i].lastSeen;
    o["age_ms"] = age;
    o["online"] = (age <= PROP_TIMEOUT_MS);
    o["ldr"] = props[i].ldr;
    o["button"] = props[i].button;
    JsonObject mp3 = o.createNestedObject("mp3");
    mp3["playing"] = props[i].mp3Playing;
    mp3["track"] = props[i].mp3Track;
    mp3["vol"] = props[i].mp3Vol;
  }
  serializeJson(doc, Serial);
  Serial.println();
}

static bool parseMac12(const char* in, uint8_t outMac[6]) {
  if (!in || strlen(in) != 12) return false;
  unsigned b[6];
  int n = sscanf(in, "%2x%2x%2x%2x%2x%2x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]);
  if (n != 6) return false;
  for (int i = 0; i < 6; i++) outMac[i] = (uint8_t)b[i];
  return true;
}

static void handleSerialJson(const char* json) {
  StaticJsonDocument<1024> doc;
  if (deserializeJson(doc, json)) return;
  JsonObject root = doc.as<JsonObject>();
  const char* cmd = root["cmd"];
  if (!cmd) return;

  if (strcmp(cmd, "list") == 0) {
    printPropsJson();
    return;
  }

  if (strcmp(cmd, "prop") == 0) {
    JsonVariant targets = root["targets"];
    JsonObject payload = root["payload"];
    if (payload.isNull()) return;
    String payloadStr;
    payloadStr.reserve(measureJson(payload) + 4);
    serializeJson(payload, payloadStr);

    auto sendToTarget = [&](const char* t) {
      if (!t || !*t) return;
      uint8_t mac[6];
      if (parseMac12(t, mac)) {
        (void)sendEspNowCmdJsonToMac(mac, payloadStr.c_str(), payloadStr.length());
        return;
      }
      int idx = findPropByName(t);
      if (idx >= 0 && isOnline(props[idx])) {
        (void)sendEspNowCmdJsonToMac(props[idx].mac, payloadStr.c_str(), payloadStr.length());
      }
    };

    if (targets.is<JsonArray>()) {
      for (JsonVariant v : targets.as<JsonArray>()) sendToTarget(v.as<const char*>());
    } else if (targets.is<const char*>()) {
      sendToTarget(targets.as<const char*>());
    }
    return;
  }

  if (strcmp(cmd, "wled") == 0) {
    const char* host = root["host"];
    JsonObject state = root["state"];
    if (!host || state.isNull()) return;
    (void)postWledJsonState(host, state);
    return;
  }
}

// --------------------------------- Arduino --------------------------------
void setup() {
  Serial.begin(115200);
  delay(50);

  // Wi-Fi optional
  if (WIFI_SSID && WIFI_SSID[0]) {
    WiFi.mode(WIFI_MODE_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  } else {
    WiFi.mode(WIFI_MODE_STA);
    WiFi.disconnect(true);
  }

  // Lock ESP-NOW to a known channel
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println(F("{\"error\":\"esp_now_init_failed\"}"));
    return;
  }
  esp_now_register_recv_cb(onEspNowRecv);

  // Ensure broadcast peer exists (so you can broadcast later if you want)
  uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  ensurePeer(bcast);

  Serial.println(F("{\"status\":\"brain_ready\"}"));
}

void loop() {
  // Read one JSON line from Serial
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (lineBuf.length()) {
        handleSerialJson(lineBuf.c_str());
        lineBuf = "";
      }
    } else {
      if (lineBuf.length() < 1200) lineBuf += c;
    }
  }

  // Lazy prune of very stale entries
  const uint32_t pruneAfter = PROP_TIMEOUT_MS * 6UL;
  for (uint8_t i = 0; i < MAX_PROPS; i++) {
    if (!props[i].present) continue;
    if ((millis() - props[i].lastSeen) > pruneAfter) props[i].present = false;
  }

  delay(2);
}

