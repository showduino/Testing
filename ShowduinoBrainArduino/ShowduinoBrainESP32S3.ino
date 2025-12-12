#include <Arduino.h>

// ===================== USER CONFIG (Arduino IDE) =====================
// Pairing code (must match props). 0 disables filtering (not recommended).
static const uint32_t PAIR_CODE = 12345;

// ESP-NOW channel (must match props). If your props are locked to channel 1, set 1 here.
static const uint8_t ESPNOW_CHANNEL = 1;

// Prop offline threshold
static const uint32_t PROP_TIMEOUT_MS = 5000;

// Run Brain as Wi-Fi AP for UI (recommended). AP channel should match ESPNOW_CHANNEL.
static const bool WIFI_AP_ENABLE = true;
static const char* WIFI_AP_SSID = "ShowduinoBrain";
static const char* WIFI_AP_PASS = "showduino"; // must be 8+ chars or "" for open

// Optional Wi-Fi (for controlling stock WLED devices via /json/state).
// Set SSID to "" to disable Wi-Fi entirely.
static const char* WIFI_SSID = "";
static const char* WIFI_PASS = "";

// SD card (recommended for show storage)
// If you have an SD slot wired to SD_MMC (4-bit), set this true.
static const bool USE_SD_MMC = false;
// SPI SD pins (only used when USE_SD_MMC=false)
static const int SD_SPI_CS   = -1;
static const int SD_SPI_SCK  = -1;
static const int SD_SPI_MOSI = -1;
static const int SD_SPI_MISO = -1;

// =====================================================================

#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <SPI.h>
#include <SD.h>
#include <SD_MMC.h>

#include <ArduinoJson.h>
#include "showduino_protocol.h"

// ----------------------------- Web server -----------------------------
static WebServer server(80);

// ------------------------------ SD helper -----------------------------
static bool sdOk = false;
static fs::FS* showFS = nullptr;
static const char* SHOW_DIR = "/shows";

static bool initStorage() {
  // Try SD_MMC first if enabled
  if (USE_SD_MMC) {
    if (SD_MMC.begin("/sdcard", true /* mode1bit */)) {
      showFS = &SD_MMC;
      sdOk = true;
      return true;
    }
  }

  // SPI SD fallback
  if (SD_SPI_CS >= 0 && SD_SPI_SCK >= 0 && SD_SPI_MOSI >= 0 && SD_SPI_MISO >= 0) {
    SPI.begin(SD_SPI_SCK, SD_SPI_MISO, SD_SPI_MOSI, SD_SPI_CS);
    if (SD.begin(SD_SPI_CS)) {
      showFS = &SD;
      sdOk = true;
      return true;
    }
  }

  sdOk = false;
  showFS = nullptr;
  return false;
}

static bool ensureShowDir() {
  if (!sdOk || !showFS) return false;
  if (showFS->exists(SHOW_DIR)) return true;
  return showFS->mkdir(SHOW_DIR);
}

static String sanitizeShowName(const String& in) {
  String s = in;
  s.trim();
  if (!s.length()) return "";
  // allow [A-Za-z0-9_-]
  String out;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s.charAt(i);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') out += c;
  }
  if (!out.length()) return "";
  return out;
}

static String showPathForName(const String& name) {
  return String(SHOW_DIR) + "/" + name + ".json";
}

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

// ---------------------------- Show runner ------------------------------
// Show JSON format:
// {
//   "name": "Haunt1",
//   "events": [
//     {"t":0, "type":"wled", "host":"192.168.1.50", "state":{...}},
//     {"t":5000, "type":"prop", "targets":["LanternA"], "payload":{...}}
//   ]
// }
static bool showLoaded = false;
static bool showPlaying = false;
static uint32_t showStartMs = 0;
static size_t nextEventIdx = 0;
static DynamicJsonDocument showDoc(64 * 1024);

static void stopShow() {
  showPlaying = false;
  showLoaded = false;
  nextEventIdx = 0;
  showDoc.clear();
}

static bool loadShowByName(const String& rawName) {
  if (!sdOk || !showFS) return false;
  String name = sanitizeShowName(rawName);
  if (!name.length()) return false;
  String path = showPathForName(name);
  if (!showFS->exists(path)) return false;

  File f = showFS->open(path, "r");
  if (!f) return false;

  showDoc.clear();
  DeserializationError err = deserializeJson(showDoc, f);
  f.close();
  if (err) return false;

  // Basic validation
  JsonObject root = showDoc.as<JsonObject>();
  if (root.isNull()) return false;
  if (!root["events"].is<JsonArray>()) return false;

  showLoaded = true;
  return true;
}

static void startShow() {
  if (!showLoaded) return;
  showPlaying = true;
  showStartMs = millis();
  nextEventIdx = 0;
}

static void runShowTick() {
  if (!showPlaying || !showLoaded) return;
  JsonArray events = showDoc["events"].as<JsonArray>();
  if (events.isNull()) { stopShow(); return; }

  const uint32_t now = millis();
  const uint32_t elapsed = now - showStartMs;

  while (nextEventIdx < events.size()) {
    JsonObject ev = events[nextEventIdx].as<JsonObject>();
    if (ev.isNull()) { nextEventIdx++; continue; }
    const uint32_t t = ev["t"] | 0;
    if (t > elapsed) break;

    const char* type = ev["type"] | "";
    if (strcmp(type, "prop") == 0) {
      JsonVariant targets = ev["targets"];
      JsonObject payload = ev["payload"];
      if (!payload.isNull()) {
        String payloadStr;
        payloadStr.reserve(measureJson(payload) + 4);
        serializeJson(payload, payloadStr);

        auto sendToTarget = [&](const char* tname) {
          if (!tname || !*tname) return;
          uint8_t mac[6];
          if (parseMac12(tname, mac)) { (void)sendEspNowCmdJsonToMac(mac, payloadStr.c_str(), payloadStr.length()); return; }
          int idx = findPropByName(tname);
          if (idx >= 0 && isOnline(props[idx])) (void)sendEspNowCmdJsonToMac(props[idx].mac, payloadStr.c_str(), payloadStr.length());
        };

        if (targets.is<JsonArray>()) for (JsonVariant v : targets.as<JsonArray>()) sendToTarget(v.as<const char*>());
        else if (targets.is<const char*>()) sendToTarget(targets.as<const char*>());
      }
    } else if (strcmp(type, "wled") == 0) {
      const char* host = ev["host"] | "";
      JsonObject state = ev["state"];
      if (host && *host && !state.isNull()) (void)postWledJsonState(host, state);
    }

    nextEventIdx++;
  }

  if (nextEventIdx >= events.size()) {
    showPlaying = false; // finished
  }
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
// 4) Show control (requires SD):
// {"cmd":"shows"}                              // list shows
// {"cmd":"show_get","name":"Haunt1"}           // fetch show json
// {"cmd":"show_save","name":"Haunt1","show":{...}}
// {"cmd":"play","name":"Haunt1"}
// {"cmd":"stop"}
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

static void printShowsJson() {
  StaticJsonDocument<1024> doc;
  JsonArray arr = doc.createNestedArray("shows");
  if (sdOk && showFS && showFS->exists(SHOW_DIR)) {
    File dir = showFS->open(SHOW_DIR);
    if (dir) {
      File f = dir.openNextFile();
      while (f) {
        if (!f.isDirectory()) {
          String n = f.name();
          if (n.endsWith(".json")) {
            JsonObject o = arr.createNestedObject();
            o["name"] = n.substring(n.lastIndexOf('/') + 1, n.length() - 5);
            o["size"] = (uint32_t)f.size();
          }
        }
        f = dir.openNextFile();
      }
      dir.close();
    }
  }
  doc["sd"] = sdOk;
  doc["playing"] = showPlaying;
  serializeJson(doc, Serial);
  Serial.println();
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

  if (strcmp(cmd, "shows") == 0) {
    printShowsJson();
    return;
  }

  if (strcmp(cmd, "play") == 0) {
    const char* name = root["name"];
    if (name && loadShowByName(name)) startShow();
    return;
  }

  if (strcmp(cmd, "stop") == 0) {
    stopShow();
    return;
  }
}

// ------------------------------ Web UI --------------------------------
static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Showduino Brain</title>
  <style>
    body{font-family:system-ui,Segoe UI,Roboto,Arial;margin:16px;background:#111;color:#eee}
    button,input,textarea{font:inherit}
    .row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}
    .card{border:1px solid #333;padding:12px;border-radius:8px;background:#161616;margin:12px 0}
    .mono{font-family:ui-monospace, SFMono-Regular, Menlo, monospace; white-space:pre-wrap}
  </style>
  <script>
    async function j(url, opt){ const r=await fetch(url,opt); const t=await r.text(); try{return JSON.parse(t)}catch(e){return {error:"bad_json",raw:t}}}
    async function refresh(){
      document.getElementById('props').textContent = JSON.stringify(await j('/api/props'),null,2);
      document.getElementById('shows').textContent = JSON.stringify(await j('/api/shows'),null,2);
      document.getElementById('status').textContent = JSON.stringify(await j('/api/status'),null,2);
    }
    async function play(){
      const n=document.getElementById('showname').value;
      await j('/api/play?name='+encodeURIComponent(n),{method:'POST'});
      refresh();
    }
    async function stop(){
      await j('/api/stop',{method:'POST'}); refresh();
    }
    async function save(){
      const n=document.getElementById('showname').value;
      const body=document.getElementById('showjson').value;
      await fetch('/api/show?name='+encodeURIComponent(n),{method:'POST',headers:{'Content-Type':'application/json'},body});
      refresh();
    }
    async function load(){
      const n=document.getElementById('showname').value;
      const x=await fetch('/api/show?name='+encodeURIComponent(n)); document.getElementById('showjson').value=await x.text();
    }
    window.addEventListener('load',()=>{refresh(); setInterval(refresh,1000);});
  </script>
</head>
<body>
  <h2>Showduino Brain (ESP32-S3)</h2>
  <div class="card">
    <div class="row">
      <label>Show:</label>
      <input id="showname" value="Haunt1">
      <button onclick="load()">Load</button>
      <button onclick="save()">Save</button>
      <button onclick="play()">Play</button>
      <button onclick="stop()">Stop</button>
    </div>
    <p>Show JSON (saved to SD under <span class="mono">/shows/&lt;name&gt;.json</span>):</p>
    <textarea id="showjson" style="width:100%;height:220px" class="mono">
{"name":"Haunt1","events":[
  {"t":0,"type":"prop","targets":["LanternA"],"payload":{"cmd":"mp3","action":"play","track":3,"volume":20}},
  {"t":0,"type":"wled","host":"192.168.1.50","state":{"on":true,"ps":1}},
  {"t":5000,"type":"prop","targets":["LanternA"],"payload":{"cmd":"mp3","action":"stop"}}
]}
    </textarea>
  </div>
  <div class="card">
    <h3>Status</h3>
    <pre id="status" class="mono"></pre>
  </div>
  <div class="card">
    <h3>Props</h3>
    <pre id="props" class="mono"></pre>
  </div>
  <div class="card">
    <h3>Shows</h3>
    <pre id="shows" class="mono"></pre>
  </div>
</body>
</html>
)HTML";

static void apiSendJson(const String& json) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

static void apiSendError(const char* msg, int code = 400) {
  StaticJsonDocument<256> doc;
  doc["error"] = msg;
  String out;
  serializeJson(doc, out);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", out);
}

static void handleApiProps() {
  StaticJsonDocument<2048> doc;
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
  String out;
  serializeJson(doc, out);
  apiSendJson(out);
}

static void handleApiStatus() {
  StaticJsonDocument<512> doc;
  doc["sd"] = sdOk;
  doc["show_loaded"] = showLoaded;
  doc["playing"] = showPlaying;
  doc["next_event"] = (uint32_t)nextEventIdx;
  doc["uptime_ms"] = (uint32_t)millis();
  doc["wifi_sta"] = (WiFi.status() == WL_CONNECTED);
  doc["ap_ip"] = WIFI_AP_ENABLE ? WiFi.softAPIP().toString() : "";
  doc["sta_ip"] = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "";
  String out;
  serializeJson(doc, out);
  apiSendJson(out);
}

static void handleApiShows() {
  StaticJsonDocument<2048> doc;
  doc["sd"] = sdOk;
  JsonArray arr = doc.createNestedArray("shows");
  if (sdOk && showFS && showFS->exists(SHOW_DIR)) {
    File dir = showFS->open(SHOW_DIR);
    if (dir) {
      File f = dir.openNextFile();
      while (f) {
        if (!f.isDirectory()) {
          String n = f.name();
          if (n.endsWith(".json")) {
            JsonObject o = arr.createNestedObject();
            o["name"] = n.substring(n.lastIndexOf('/') + 1, n.length() - 5);
            o["size"] = (uint32_t)f.size();
          }
        }
        f = dir.openNextFile();
      }
      dir.close();
    }
  }
  String out;
  serializeJson(doc, out);
  apiSendJson(out);
}

static void handleApiShowGet() {
  if (!sdOk || !showFS) return apiSendError("sd_not_ready", 500);
  String name = sanitizeShowName(server.arg("name"));
  if (!name.length()) return apiSendError("bad_name");
  String path = showPathForName(name);
  if (!showFS->exists(path)) return apiSendError("not_found", 404);
  File f = showFS->open(path, "r");
  if (!f) return apiSendError("open_failed", 500);
  server.sendHeader("Cache-Control", "no-store");
  server.streamFile(f, "application/json");
  f.close();
}

static void handleApiShowSave() {
  if (!sdOk || !showFS) return apiSendError("sd_not_ready", 500);
  if (!ensureShowDir()) return apiSendError("mkdir_failed", 500);
  String name = sanitizeShowName(server.arg("name"));
  if (!name.length()) return apiSendError("bad_name");
  String body = server.arg("plain");
  if (!body.length()) return apiSendError("empty_body");

  // Validate JSON quickly
  StaticJsonDocument<1024> tmp;
  if (deserializeJson(tmp, body)) return apiSendError("invalid_json");

  String path = showPathForName(name);
  File f = showFS->open(path, "w");
  if (!f) return apiSendError("open_failed", 500);
  f.print(body);
  f.close();
  apiSendJson("{\"ok\":true}");
}

static void handleApiPlay() {
  if (!sdOk) return apiSendError("sd_not_ready", 500);
  String name = sanitizeShowName(server.arg("name"));
  if (!name.length()) return apiSendError("bad_name");
  if (!loadShowByName(name)) return apiSendError("load_failed", 500);
  startShow();
  apiSendJson("{\"ok\":true}");
}

static void handleApiStop() {
  stopShow();
  apiSendJson("{\"ok\":true}");
}

// --------------------------------- Arduino --------------------------------
void setup() {
  Serial.begin(115200);
  delay(50);

  // Wi-Fi (AP + optional STA)
  WiFi.mode(WIFI_MODE_APSTA);
  if (WIFI_AP_ENABLE) {
    WiFi.softAP(WIFI_AP_SSID, (WIFI_AP_PASS && WIFI_AP_PASS[0]) ? WIFI_AP_PASS : nullptr, ESPNOW_CHANNEL);
  } else {
    WiFi.softAPdisconnect(true);
  }
  if (WIFI_SSID && WIFI_SSID[0]) {
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }

  // Lock ESP-NOW to a known channel (must match AP channel)
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println(F("{\"error\":\"esp_now_init_failed\"}"));
    return;
  }
  esp_now_register_recv_cb(onEspNowRecv);

  // Ensure broadcast peer exists (so you can broadcast later if you want)
  uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  ensurePeer(bcast);

  // Storage
  initStorage();
  if (sdOk) ensureShowDir();

  // Web routes
  server.on("/", HTTP_GET, [](){ server.send(200, "text/html", FPSTR(INDEX_HTML)); });
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/props", HTTP_GET, handleApiProps);
  server.on("/api/shows", HTTP_GET, handleApiShows);
  server.on("/api/show", HTTP_GET, handleApiShowGet);
  server.on("/api/show", HTTP_POST, handleApiShowSave);
  server.on("/api/play", HTTP_POST, handleApiPlay);
  server.on("/api/stop", HTTP_POST, handleApiStop);
  server.onNotFound([](){ apiSendError("not_found", 404); });
  server.begin();

  Serial.println(F("{\"status\":\"brain_ready\"}"));
}

void loop() {
  server.handleClient();
  runShowTick();

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

