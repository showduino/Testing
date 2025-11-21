#include <WiFi.h>
#include <SPIFFS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <vector>
#include <cmath>

#define LED_PIN 18
#define LED_ROWS 10
#define LED_COLS 21
#define LED_COUNT (LED_ROWS * LED_COLS)

#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

enum class DeviceMode : uint8_t { STATIC = 0, ANIMATION = 1, EFFECT = 2 };

struct Pixel {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

struct FrameBuffer {
  Pixel pixels[LED_COUNT];
};

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

Pixel frontBuffer[LED_COUNT];
Pixel backBuffer[LED_COUNT];
volatile bool frameReady = false;
volatile uint8_t pendingBrightness = 128;
uint8_t activeBrightness = 128;

DeviceMode currentMode = DeviceMode::STATIC;
uint8_t deviceSpeed = 50;

std::vector<FrameBuffer> animationFrames;
uint8_t animationFps = 24;
bool animationLoop = true;
size_t animationCursor = 0;
uint32_t lastAnimationTick = 0;

String activeEffect = "rainbow";
DynamicJsonDocument effectParams(512);
uint32_t effectTick = 0;
uint32_t lastEffectUpdate = 0;

uint32_t statsTimer = 0;
uint32_t framesPushed = 0;
uint32_t packetsReceived = 0;

void handleWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                          AwsEventType type, void *arg, uint8_t *data, size_t len);
void handleBinaryPacket(uint8_t *data, size_t len, AsyncWebSocketClient *client);
void handleTextPacket(const char *payload, size_t len);
void broadcastStats();
void saveAnimationHandler(AsyncWebServerRequest *request, JsonVariant &json);
void handleAnimationList(AsyncWebServerRequest *request);
void handleAnimationFetch(AsyncWebServerRequest *request);
void ensureAnimationFolder();
void pushFrontBuffer();
void copyBackToFront();
void handleAnimationPlayback();
void handleEffectEngine();
void renderEffectFrame();
void renderRainbow();
void renderFire();
void renderTwinkle();
void renderSnow();
void renderGlitch();
void renderMeteor();
float getEffectParam(const char *key, float fallback);
uint16_t xyToIndex(uint8_t x, uint8_t y);
Pixel hexToPixel(const char *hex);

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println(F("[BOOT] Starting Matrix Designer firmware"));

  if (!SPIFFS.begin(true)) {
    Serial.println(F("[SPIFFS] Mount failed"));
    while (true) {
      delay(1000);
    }
  }
  ensureAnimationFolder();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[WIFI] Connecting to %s\n", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n[WIFI] Connected. IP: %s\n", WiFi.localIP().toString().c_str());

  strip.begin();
  strip.clear();
  strip.show();

  ws.onEvent(handleWebSocketEvent);
  server.addHandler(&ws);

  server.serveStatic("/web", SPIFFS, "/web");
  server.serveStatic("/", SPIFFS, "/web/").setDefaultFile("index.html");

  server.on("/api/animations", HTTP_GET, handleAnimationList);
  server.onNotFound(handleAnimationFetch);

  auto *saveHandler =
      new AsyncCallbackJsonWebHandler("/api/animations", saveAnimationHandler);
  saveHandler->setMethod(HTTP_POST);
  server.addHandler(saveHandler);

  server.begin();
  randomSeed(esp_random());
  effectParams.clear();
  effectParams["shift"] = 0;
  effectParams["saturation"] = 90;
  effectParams["brightness"] = 80;
}

void loop() {
  ws.cleanupClients();

  if (frameReady) {
    copyBackToFront();
    pushFrontBuffer();
  }

  switch (currentMode) {
    case DeviceMode::ANIMATION:
      handleAnimationPlayback();
      break;
    case DeviceMode::EFFECT:
      handleEffectEngine();
      break;
    default:
      break;
  }

  if (millis() - statsTimer > 1000) {
    statsTimer = millis();
    broadcastStats();
    framesPushed = 0;
  }
}

void handleWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                          AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("[WS] Client %u connected\n", client->id());
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("[WS] Client %u disconnected\n", client->id());
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = static_cast<AwsFrameInfo *>(arg);
    if (info->final && info->index == 0) {
        if (info->opcode == WS_TEXT) {
          String message;
          message.reserve(len + 1);
          for (size_t i = 0; i < len; i++) {
            message += static_cast<char>(data[i]);
          }
          handleTextPacket(message.c_str(), message.length());
      } else if (info->opcode == WS_BINARY) {
        handleBinaryPacket(data, len, client);
      }
    }
  }
}

void handleBinaryPacket(uint8_t *data, size_t len, AsyncWebSocketClient *client) {
  (void)client;
  if (len < 2) {
    return;
  }
  const uint8_t command = data[0];
  packetsReceived++;
  switch (command) {
    case 0x01: {  // Frame update
      const uint8_t brightness = data[1];
      const size_t expected = 2 + LED_COUNT * 3;
      if (len != expected) {
        Serial.printf("[WS] Frame payload mismatch (%u vs %u)\n",
                      static_cast<unsigned>(len),
                      static_cast<unsigned>(expected));
        return;
      }
      for (size_t i = 0; i < LED_COUNT; i++) {
        const size_t offset = 2 + i * 3;
        backBuffer[i].r = data[offset];
        backBuffer[i].g = data[offset + 1];
        backBuffer[i].b = data[offset + 2];
      }
      pendingBrightness = brightness;
      frameReady = true;
      currentMode = DeviceMode::STATIC;
      break;
    }
    case 0x02: {  // Device settings
      if (len < 4) return;
      activeBrightness = data[1];
      deviceSpeed = data[2];
      const uint8_t mode = data[3];
      if (mode <= 2) {
        currentMode = static_cast<DeviceMode>(mode);
      }
      Serial.printf("[SETTINGS] Brightness=%u Speed=%u Mode=%u\n", activeBrightness,
                    deviceSpeed, mode);
      break;
    }
    default:
      Serial.printf("[WS] Unknown binary cmd %u\n", command);
      break;
  }
}

void handleTextPacket(const char *payload, size_t len) {
  StaticJsonDocument<16384> doc;
  DeserializationError err = deserializeJson(doc, payload, len);
  if (err) {
    Serial.printf("[JSON] Parse error: %s\n", err.c_str());
    return;
  }
  const char *type = doc["type"] | "";
  if (strcmp(type, "animation") == 0) {
    animationFps = doc["fps"] | 24;
    animationLoop = doc["loop"] | true;
    animationFrames.clear();
    JsonArray frames = doc["frames"].as<JsonArray>();
    animationFrames.reserve(frames.size());
    for (JsonVariant frameVariant : frames) {
      JsonArray colors = frameVariant.as<JsonArray>();
      if (colors.size() != LED_COUNT) continue;
      FrameBuffer fb{};
      for (size_t i = 0; i < LED_COUNT; i++) {
        const char *hex = colors[i];
        Pixel px = hexToPixel(hex ? hex : "#000000");
        fb.pixels[i] = px;
      }
      animationFrames.push_back(fb);
    }
    animationCursor = 0;
    currentMode = DeviceMode::ANIMATION;
    Serial.printf("[ANIM] Loaded %u frames @ %u FPS\n",
                  static_cast<unsigned>(animationFrames.size()), animationFps);
  } else if (strcmp(type, "effect") == 0) {
    activeEffect = doc["effect"] | "rainbow";
    effectParams.clear();
    if (doc["params"].is<JsonObject>()) {
      JsonObject params = doc["params"].as<JsonObject>();
      for (JsonPair kv : params) {
        effectParams[kv.key().c_str()] = kv.value().as<float>();
      }
    }
    currentMode = DeviceMode::EFFECT;
    Serial.printf("[EFFECT] Mode %s activated\n", activeEffect.c_str());
  }
}

void copyBackToFront() {
  noInterrupts();
  memcpy(frontBuffer, backBuffer, sizeof(frontBuffer));
  activeBrightness = pendingBrightness;
  frameReady = false;
  interrupts();
}

void pushFrontBuffer() {
  strip.setBrightness(activeBrightness);
  for (size_t i = 0; i < LED_COUNT; i++) {
    strip.setPixelColor(i, strip.Color(frontBuffer[i].r, frontBuffer[i].g,
                                       frontBuffer[i].b));
  }
  strip.show();
  framesPushed++;
}

void handleAnimationPlayback() {
  if (animationFrames.empty()) return;
  const uint32_t interval = max<uint32_t>(10, 1000 / max<uint8_t>(1, animationFps));
  const uint32_t now = millis();
  if (now - lastAnimationTick < interval) return;
  lastAnimationTick = now;
  memcpy(frontBuffer, animationFrames[animationCursor].pixels, sizeof(frontBuffer));
  pushFrontBuffer();
  animationCursor++;
  if (animationCursor >= animationFrames.size()) {
    if (animationLoop) {
      animationCursor = 0;
    } else {
      animationCursor = animationFrames.size() - 1;
    }
  }
}

void handleEffectEngine() {
  const uint32_t speedScaled = map(deviceSpeed, 1, 100, 120, 20);
  if (millis() - lastEffectUpdate < speedScaled) return;
  lastEffectUpdate = millis();
  renderEffectFrame();
  pushFrontBuffer();
  effectTick++;
}

void renderEffectFrame() {
  if (activeEffect == "fire") {
    renderFire();
  } else if (activeEffect == "twinkle") {
    renderTwinkle();
  } else if (activeEffect == "snow") {
    renderSnow();
  } else if (activeEffect == "glitch") {
    renderGlitch();
  } else if (activeEffect == "meteor") {
    renderMeteor();
  } else {
    renderRainbow();
  }
}

void renderRainbow() {
  const float shift = getEffectParam("shift", 0.0f) + (effectTick % 360);
  const float saturation = getEffectParam("saturation", 90.0f);
  const float brightness = getEffectParam("brightness", 80.0f);
  for (uint8_t y = 0; y < LED_ROWS; y++) {
    for (uint8_t x = 0; x < LED_COLS; x++) {
      const float hue = fmod((static_cast<float>(x) / LED_COLS) * 360.0f + shift, 360.0f);
      const uint16_t idx = xyToIndex(x, y);
      const float s = saturation / 100.0f;
      const float v = brightness / 100.0f;
      const float c = v * s;
      const float hPrime = hue / 60.0f;
      const float xComp = c * (1 - fabs(fmod(hPrime, 2.0f) - 1));
      float r = 0, g = 0, b = 0;
      if (hPrime < 1) {
        r = c;
        g = xComp;
      } else if (hPrime < 2) {
        r = xComp;
        g = c;
      } else if (hPrime < 3) {
        g = c;
        b = xComp;
      } else if (hPrime < 4) {
        g = xComp;
        b = c;
      } else if (hPrime < 5) {
        r = xComp;
        b = c;
      } else {
        r = c;
        b = xComp;
      }
      const float m = v - c;
      frontBuffer[idx].r = static_cast<uint8_t>((r + m) * 255);
      frontBuffer[idx].g = static_cast<uint8_t>((g + m) * 255);
      frontBuffer[idx].b = static_cast<uint8_t>((b + m) * 255);
    }
  }
}

void renderFire() {
  const float intensity = getEffectParam("intensity", 70.0f) / 100.0f;
  const float flicker = getEffectParam("flicker", 4.0f);
  for (int y = LED_ROWS - 1; y >= 0; y--) {
    for (int x = 0; x < LED_COLS; x++) {
      float heat = (static_cast<float>(y) / LED_ROWS) * intensity;
      heat += (random(0, 100) / 100.0f) * (flicker / 10.0f);
      heat = constrain(heat, 0.0f, 1.0f);
      const uint16_t idx = xyToIndex(x, y);
      frontBuffer[idx].r = static_cast<uint8_t>(min(255.0f, 180 + heat * 75));
      frontBuffer[idx].g = static_cast<uint8_t>(min(255.0f, heat * 120));
      frontBuffer[idx].b = static_cast<uint8_t>(heat * 40);
    }
  }
}

void renderTwinkle() {
  const int density = static_cast<int>(getEffectParam("density", 12.0f));
  const int hue = static_cast<int>(getEffectParam("hue", 210.0f));
  memset(frontBuffer, 0, sizeof(frontBuffer));
  for (int i = 0; i < density; i++) {
    const uint8_t x = random(0, LED_COLS);
    const uint8_t y = random(0, LED_ROWS);
    const uint16_t idx = xyToIndex(x, y);
    uint32_t color = strip.ColorHSV((hue * 65535) / 360, 80 * 255 / 100, random(150, 255));
    frontBuffer[idx].r = (color >> 16) & 0xFF;
    frontBuffer[idx].g = (color >> 8) & 0xFF;
    frontBuffer[idx].b = color & 0xFF;
  }
}

void renderSnow() {
  memset(frontBuffer, 0, sizeof(frontBuffer));
  const int count = static_cast<int>(getEffectParam("count", 24.0f));
  for (int i = 0; i < count; i++) {
    const uint8_t x = random(0, LED_COLS);
    const uint8_t y = random(0, LED_ROWS);
    const uint16_t idx = xyToIndex(x, y);
    frontBuffer[idx].r = 220;
    frontBuffer[idx].g = 235;
    frontBuffer[idx].b = 255;
  }
}

void renderGlitch() {
  memset(frontBuffer, 0, sizeof(frontBuffer));
  const int blocks = static_cast<int>(getEffectParam("blocks", 4.0f));
  const int chaos = static_cast<int>(getEffectParam("chaos", 6.0f));
  for (int i = 0; i < blocks; i++) {
    const uint8_t w = max(1, random(1, chaos));
    const uint8_t h = max(1, random(1, chaos));
    const uint8_t x0 = random(0, max(1, LED_COLS - w));
    const uint8_t y0 = random(0, max(1, LED_ROWS - h));
    uint32_t color =
        strip.Color(random(50, 255), random(50, 255), random(50, 255));
    for (uint8_t y = y0; y < y0 + h && y < LED_ROWS; y++) {
      for (uint8_t x = x0; x < x0 + w && x < LED_COLS; x++) {
        const uint16_t idx = xyToIndex(x, y);
        frontBuffer[idx].r = (color >> 16) & 0xFF;
        frontBuffer[idx].g = (color >> 8) & 0xFF;
        frontBuffer[idx].b = color & 0xFF;
      }
    }
  }
}

void renderMeteor() {
  memset(frontBuffer, 0, sizeof(frontBuffer));
  const int length = static_cast<int>(getEffectParam("length", 8.0f));
  const int hue = static_cast<int>(getEffectParam("hue", 180.0f));
  const int trails = static_cast<int>(getEffectParam("trails", 2.0f));
  const uint8_t start = (effectTick / 2) % LED_COLS;
  for (int i = 0; i < length; i++) {
    const uint8_t y = min(LED_ROWS - 1, i);
    const uint8_t x = (start + i) % LED_COLS;
    const uint16_t idx = xyToIndex(x, y);
    uint32_t color = strip.ColorHSV((hue * 65535) / 360, 200, 255 - i * (255 / length));
    frontBuffer[idx].r = (color >> 16) & 0xFF;
    frontBuffer[idx].g = (color >> 8) & 0xFF;
    frontBuffer[idx].b = color & 0xFF;
    for (int t = 1; t <= trails; t++) {
      const int yy = y + t;
      if (yy >= LED_ROWS) break;
      const uint16_t tidx = xyToIndex(x, yy);
      const uint8_t fade = max(0, (255 - i * (255 / length)) - t * 40);
      frontBuffer[tidx].r = (fade * ((color >> 16) & 0xFF)) / 255;
      frontBuffer[tidx].g = (fade * ((color >> 8) & 0xFF)) / 255;
      frontBuffer[tidx].b = (fade * (color & 0xFF)) / 255;
    }
  }
}

float getEffectParam(const char *key, float fallback) {
  if (effectParams.containsKey(key)) {
    return effectParams[key].as<float>();
  }
  return fallback;
}

uint16_t xyToIndex(uint8_t x, uint8_t y) {
  if (y % 2 == 0) {
    return y * LED_COLS + x;
  }
  return y * LED_COLS + (LED_COLS - 1 - x);
}

Pixel hexToPixel(const char *hex) {
  Pixel px{0, 0, 0};
  if (!hex || hex[0] != '#') return px;
  uint32_t value = strtoul(hex + 1, nullptr, 16);
  if (strlen(hex) == 7) {
    px.r = (value >> 16) & 0xFF;
    px.g = (value >> 8) & 0xFF;
    px.b = value & 0xFF;
  }
  return px;
}

void broadcastStats() {
  StaticJsonDocument<128> doc;
  doc["fps"] = framesPushed;
  doc["packets"] = packetsReceived;
  String payload;
  serializeJson(doc, payload);
  ws.textAll(payload);
}

void saveAnimationHandler(AsyncWebServerRequest *request, JsonVariant &json) {
  if (!json.is<JsonObject>()) {
    request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }
  JsonObject obj = json.as<JsonObject>();
  const String name = obj["name"] | "";
  if (name.isEmpty()) {
    request->send(400, "application/json", "{\"error\":\"Missing name\"}");
    return;
  }
  ensureAnimationFolder();
  String path = "/animations/" + name;
  if (!path.endsWith(".json")) {
    path += ".json";
  }
  File file = SPIFFS.open(path, FILE_WRITE);
  if (!file) {
    request->send(500, "application/json", "{\"error\":\"Cannot write file\"}");
    return;
  }
  serializeJson(obj, file);
  file.close();
  request->send(200, "application/json", "{\"status\":\"ok\"}");
  Serial.printf("[SPIFFS] Saved animation %s\n", path.c_str());
}

void handleAnimationList(AsyncWebServerRequest *request) {
  ensureAnimationFolder();
  File root = SPIFFS.open("/animations");
  if (!root || !root.isDirectory()) {
    request->send(200, "application/json", "[]");
    return;
  }
  String json = "[";
  File file = root.openNextFile();
  bool first = true;
  while (file) {
    if (!file.isDirectory()) {
      if (!first) json += ",";
      String name = String(file.name());
      name.replace("/animations/", "");
      json += "\"" + name + "\"";
      first = false;
    }
    file = root.openNextFile();
  }
  json += "]";
  request->send(200, "application/json", json);
}

void handleAnimationFetch(AsyncWebServerRequest *request) {
  const String url = request->url();
  if (url.startsWith("/api/animations/")) {
    String name = url.substring(strlen("/api/animations/"));
    if (name.isEmpty()) {
      request->send(404, "application/json", "{\"error\":\"Not found\"}");
      return;
    }
    String path = "/animations/" + name;
    if (!SPIFFS.exists(path)) {
      path += ".json";
    }
    if (!SPIFFS.exists(path)) {
      request->send(404, "application/json", "{\"error\":\"Not found\"}");
      return;
    }
    request->send(SPIFFS, path, "application/json");
    return;
  }
  request->send(404, "text/plain", "Not found");
}

void ensureAnimationFolder() {
  if (!SPIFFS.exists("/animations")) {
    SPIFFS.mkdir("/animations");
  }
}
