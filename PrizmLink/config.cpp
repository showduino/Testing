#include <algorithm>
#include "config.h"
#include "sd_logger.h"
#include "debug_utils.h"

namespace Prizm {
namespace Config {

PrizmConfig active {};
RuntimeStats stats {};

static void logLoadError(const char *msg) {
  Debug::error("Config", msg);
}

void applyDefaults(PrizmConfig &cfg) {
  cfg = PrizmConfig{}; // value initialize with defaults declared in structs
}

static bool loadJson(fs::FS &fs, const char *path, PrizmConfig &cfg) {
  File f = fs.open(path, "r");
  if (!f) {
    Debug::warn("Config", "Missing %s, using defaults", path);
    return false;
  }

  DynamicJsonDocument doc(3072);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    Debug::error("Config", "JSON parse error: %s", err.c_str());
    return false;
  }

  JsonObject root = doc.as<JsonObject>();

  auto net = root["network"].as<JsonObject>();
  if (!net.isNull()) {
    if (net.containsKey("ssid")) cfg.network.ssid = net["ssid"].as<const char*>();
    if (net.containsKey("password")) cfg.network.password = net["password"].as<const char*>();
    if (net.containsKey("hostname")) cfg.network.hostname = net["hostname"].as<const char*>();
    if (net.containsKey("apFallback")) cfg.network.apFallback = net["apFallback"].as<bool>();
    if (net.containsKey("dhcp")) cfg.network.useDHCP = net["dhcp"].as<bool>();
    if (net.containsKey("multicast")) cfg.network.multicast = net["multicast"].as<bool>();

    if (!cfg.network.useDHCP) {
      if (net.containsKey("ip")) cfg.network.localIp.fromString(net["ip"].as<const char*>());
      if (net.containsKey("gateway")) cfg.network.gateway.fromString(net["gateway"].as<const char*>());
      if (net.containsKey("subnet")) cfg.network.subnet.fromString(net["subnet"].as<const char*>());
      if (net.containsKey("dns")) cfg.network.dns.fromString(net["dns"].as<const char*>());
    }
  }

  auto e131 = root["e131"].as<JsonObject>();
  if (!e131.isNull()) {
    if (e131.containsKey("start")) cfg.e131.startUniverse = e131["start"].as<uint16_t>();
    if (e131.containsKey("count")) cfg.e131.universeCount = std::min<uint16_t>(e131["count"].as<uint16_t>(), kMaxUniverses);
    if (e131.containsKey("channels")) cfg.e131.channelsPerUniverse = e131["channels"].as<uint16_t>();
    if (e131.containsKey("priority")) cfg.e131.priority = e131["priority"].as<uint16_t>();
  }

  auto pixels = root["pixels"].as<JsonObject>();
  if (!pixels.isNull()) {
    if (pixels.containsKey("enabled")) cfg.pixels.enabled = pixels["enabled"].as<bool>();
    if (pixels.containsKey("count")) cfg.pixels.count = pixels["count"].as<uint16_t>();
    if (pixels.containsKey("pin")) cfg.pixels.dataPin = pixels["pin"].as<uint8_t>();
    if (pixels.containsKey("brightness")) cfg.pixels.brightness = pixels["brightness"].as<uint8_t>();
    if (pixels.containsKey("sk6812")) cfg.pixels.useWhiteChannel = pixels["sk6812"].as<bool>();
    if (pixels.containsKey("grbw")) cfg.pixels.grbwOrder = pixels["grbw"].as<bool>();
  }

  auto dmx = root["dmx"].as<JsonObject>();
  if (!dmx.isNull()) {
    if (dmx.containsKey("enabled")) cfg.dmx.enabled = dmx["enabled"].as<bool>();
    if (dmx.containsKey("channels")) cfg.dmx.channels = dmx["channels"].as<uint16_t>();
    if (dmx.containsKey("pin")) cfg.dmx.txPin = dmx["pin"].as<uint8_t>();
    if (dmx.containsKey("fps")) cfg.dmx.fps = dmx["fps"].as<uint16_t>();
  }

  auto servos = root["servos"].as<JsonObject>();
  if (!servos.isNull()) {
    if (servos.containsKey("enabled")) cfg.servos.enabled = servos["enabled"].as<bool>();
    if (servos.containsKey("address")) cfg.servos.pcaAddress = servos["address"].as<uint8_t>();
    if (servos.containsKey("sda")) cfg.servos.sda = servos["sda"].as<uint8_t>();
    if (servos.containsKey("scl")) cfg.servos.scl = servos["scl"].as<uint8_t>();
    if (servos.containsKey("joy1X")) cfg.servos.joystickXPin = servos["joy1X"].as<uint8_t>();
    if (servos.containsKey("joy1Y")) cfg.servos.joystickYPin = servos["joy1Y"].as<uint8_t>();
    if (servos.containsKey("joy2X")) cfg.servos.joystick2XPin = servos["joy2X"].as<uint8_t>();
    if (servos.containsKey("joy2Y")) cfg.servos.joystick2YPin = servos["joy2Y"].as<uint8_t>();
    if (servos.containsKey("button1")) cfg.servos.button1Pin = servos["button1"].as<uint8_t>();
    if (servos.containsKey("button2")) cfg.servos.button2Pin = servos["button2"].as<uint8_t>();
    if (servos.containsKey("button1Active")) cfg.servos.button1ActiveState = servos["button1Active"].as<int>();
    if (servos.containsKey("button2Active")) cfg.servos.button2ActiveState = servos["button2Active"].as<int>();
    if (servos.containsKey("max")) cfg.servos.maxServoAngle = servos["max"].as<float>();
    if (servos.containsKey("min")) cfg.servos.minServoAngle = servos["min"].as<float>();
    if (servos.containsKey("neutral")) cfg.servos.neutralAngle = servos["neutral"].as<float>();
  }

  auto pots = root["pots"].as<JsonObject>();
  if (!pots.isNull()) {
    if (pots.containsKey("brightness")) cfg.pots.brightnessPin = pots["brightness"].as<uint8_t>();
    if (pots.containsKey("fx")) cfg.pots.fxSpeedPin = pots["fx"].as<uint8_t>();
  }

  auto buttons = root["buttons"].as<JsonObject>();
  if (!buttons.isNull()) {
    if (buttons.containsKey("stop")) cfg.buttons.stopPin = buttons["stop"].as<uint8_t>();
    if (buttons.containsKey("cycle")) cfg.buttons.cyclePin = buttons["cycle"].as<uint8_t>();
    if (buttons.containsKey("confirm")) cfg.buttons.confirmPin = buttons["confirm"].as<uint8_t>();
    if (buttons.containsKey("activeLow")) cfg.buttons.activeLow = buttons["activeLow"].as<bool>();
  }

  auto oled = root["oled"].as<JsonObject>();
  if (!oled.isNull()) {
    if (oled.containsKey("enabled")) cfg.oled.enabled = oled["enabled"].as<bool>();
    if (oled.containsKey("sda")) cfg.oled.sda = oled["sda"].as<uint8_t>();
    if (oled.containsKey("scl")) cfg.oled.scl = oled["scl"].as<uint8_t>();
    if (oled.containsKey("address")) cfg.oled.address = oled["address"].as<uint8_t>();
  }

  auto sd = root["sd"].as<JsonObject>();
  if (!sd.isNull()) {
    if (sd.containsKey("enabled")) cfg.sd.enabled = sd["enabled"].as<bool>();
    if (sd.containsKey("useSpi")) cfg.sd.useSpi = sd["useSpi"].as<bool>();
    if (sd.containsKey("cs")) cfg.sd.csPin = sd["cs"].as<uint8_t>();
    if (sd.containsKey("root")) cfg.sd.root = sd["root"].as<const char*>();
  }

  auto web = root["web"].as<JsonObject>();
  if (!web.isNull()) {
    if (web.containsKey("enabled")) cfg.web.enabled = web["enabled"].as<bool>();
    if (web.containsKey("port")) cfg.web.port = web["port"].as<uint16_t>();
    if (web.containsKey("websocket")) cfg.web.websocket = web["websocket"].as<bool>();
  }

  auto failsafe = root["failsafe"].as<JsonObject>();
  if (!failsafe.isNull()) {
    if (failsafe.containsKey("timeout")) cfg.failsafe.timeoutMs = failsafe["timeout"].as<uint32_t>();
    if (failsafe.containsKey("enable")) cfg.failsafe.enableFx = failsafe["enable"].as<bool>();
    if (failsafe.containsKey("preset")) cfg.failsafe.fxPreset = failsafe["preset"].as<const char*>();
    if (failsafe.containsKey("floor")) cfg.failsafe.brightnessFloor = failsafe["floor"].as<uint8_t>();
  }

  return true;
}

bool load(fs::FS &fs, const char *path) {
  PrizmConfig cfg;
  applyDefaults(cfg);

  if (!loadJson(fs, path, cfg)) {
    active = cfg;
    return false;
  }

  active = cfg;
  Debug::info("Config", "Config loaded from %s", path);
  return true;
}

static void fillNetwork(JsonObject obj, const NetworkConfig &net) {
  obj["ssid"] = net.ssid;
  obj["password"] = net.password;
  obj["hostname"] = net.hostname;
  obj["apFallback"] = net.apFallback;
  obj["dhcp"] = net.useDHCP;
  obj["multicast"] = net.multicast;
  obj["ip"] = net.localIp.toString();
  obj["gateway"] = net.gateway.toString();
  obj["subnet"] = net.subnet.toString();
  obj["dns"] = net.dns.toString();
}

String toJsonString(const PrizmConfig &cfg, bool pretty) {
  DynamicJsonDocument doc(3072);

  fillNetwork(doc.createNestedObject("network"), cfg.network);

  JsonObject e131 = doc.createNestedObject("e131");
  e131["start"] = cfg.e131.startUniverse;
  e131["count"] = cfg.e131.universeCount;
  e131["channels"] = cfg.e131.channelsPerUniverse;
  e131["priority"] = cfg.e131.priority;

  JsonObject pixels = doc.createNestedObject("pixels");
  pixels["enabled"] = cfg.pixels.enabled;
  pixels["count"] = cfg.pixels.count;
  pixels["pin"] = cfg.pixels.dataPin;
  pixels["brightness"] = cfg.pixels.brightness;
  pixels["sk6812"] = cfg.pixels.useWhiteChannel;
  pixels["grbw"] = cfg.pixels.grbwOrder;

  JsonObject dmx = doc.createNestedObject("dmx");
  dmx["enabled"] = cfg.dmx.enabled;
  dmx["channels"] = cfg.dmx.channels;
  dmx["pin"] = cfg.dmx.txPin;
  dmx["fps"] = cfg.dmx.fps;

  JsonObject servos = doc.createNestedObject("servos");
  servos["enabled"] = cfg.servos.enabled;
  servos["address"] = cfg.servos.pcaAddress;
  servos["sda"] = cfg.servos.sda;
  servos["scl"] = cfg.servos.scl;
  servos["joy1X"] = cfg.servos.joystickXPin;
  servos["joy1Y"] = cfg.servos.joystickYPin;
  servos["joy2X"] = cfg.servos.joystick2XPin;
  servos["joy2Y"] = cfg.servos.joystick2YPin;
  servos["button1"] = cfg.servos.button1Pin;
  servos["button2"] = cfg.servos.button2Pin;
  servos["button1Active"] = cfg.servos.button1ActiveState;
  servos["button2Active"] = cfg.servos.button2ActiveState;
  servos["max"] = cfg.servos.maxServoAngle;
  servos["min"] = cfg.servos.minServoAngle;
  servos["neutral"] = cfg.servos.neutralAngle;

  JsonObject pots = doc.createNestedObject("pots");
  pots["brightness"] = cfg.pots.brightnessPin;
  pots["fx"] = cfg.pots.fxSpeedPin;

  JsonObject buttons = doc.createNestedObject("buttons");
  buttons["stop"] = cfg.buttons.stopPin;
  buttons["cycle"] = cfg.buttons.cyclePin;
  buttons["confirm"] = cfg.buttons.confirmPin;
  buttons["activeLow"] = cfg.buttons.activeLow;

  JsonObject oled = doc.createNestedObject("oled");
  oled["enabled"] = cfg.oled.enabled;
  oled["sda"] = cfg.oled.sda;
  oled["scl"] = cfg.oled.scl;
  oled["address"] = cfg.oled.address;

  JsonObject sd = doc.createNestedObject("sd");
  sd["enabled"] = cfg.sd.enabled;
  sd["useSpi"] = cfg.sd.useSpi;
  sd["cs"] = cfg.sd.csPin;
  sd["root"] = cfg.sd.root;

  JsonObject web = doc.createNestedObject("web");
  web["enabled"] = cfg.web.enabled;
  web["port"] = cfg.web.port;
  web["websocket"] = cfg.web.websocket;

  JsonObject failsafe = doc.createNestedObject("failsafe");
  failsafe["timeout"] = cfg.failsafe.timeoutMs;
  failsafe["enable"] = cfg.failsafe.enableFx;
  failsafe["preset"] = cfg.failsafe.fxPreset;
  failsafe["floor"] = cfg.failsafe.brightnessFloor;

  String out;
  if (pretty) serializeJsonPretty(doc, out);
  else serializeJson(doc, out);
  return out;
}

bool save(fs::FS &fs, const char *path) {
  File f = fs.open(path, FILE_WRITE);
  if (!f) {
    Debug::error("Config", "Failed to open %s for writing", path);
    return false;
  }

  String json = toJsonString(active, true);
  size_t written = f.print(json);
  f.close();
  if (written != json.length()) {
    Debug::error("Config", "Short write to %s", path);
    return false;
  }

  Debug::info("Config", "Saved configuration (%d bytes)", written);
  return true;
}

} // namespace Config
} // namespace Prizm

