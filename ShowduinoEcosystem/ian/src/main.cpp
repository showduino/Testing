#include <Arduino.h>

#include <SPI.h>
#include <Wire.h>

#include <ArduinoJson.h>

#include <showduino/config.h>
#include <showduino/pins_ian.h>
#include <showduino/proto.h>
#include <showduino/link_uart.h>

using namespace showduino;

namespace {

uint32_t g_seq = 1;
uint32_t g_last_sue_ms = 0;
bool g_sue_linked = false;

UartLink g_sue_link(Serial1, IAN_SUE_UART_RX, IAN_SUE_UART_TX, IAN_SUE_UART_BAUD);
UartLink g_kids_link(Serial2, IAN_KIDS_UART_RX, IAN_KIDS_UART_TX, IAN_KIDS_UART_BAUD);

SPIClass g_spi(FSPI);
bool g_flash_ok = false;
uint32_t g_last_inventory_ms = 0;

struct KidInfo {
  uint16_t id = 0;
  uint32_t last_ms = 0;
  bool ready = false;
};
KidInfo g_kids[8]{};

PacketHeader make_hdr(MsgType t, uint16_t dst, uint32_t id_ref = 0) {
  PacketHeader h{};
  h.version = 1;
  h.type = static_cast<uint8_t>(t);
  h.src = SHOWDUINO_NODE_ID;
  h.dst = dst;
  h.id = id_ref ? id_ref : (uint32_t)esp_random();
  h.seq = g_seq++;
  h.ts_ms = millis();
  return h;
}

bool read_jedec(uint8_t cs, uint8_t &mfg, uint8_t &mem, uint8_t &cap) {
  pinMode(cs, OUTPUT);
  digitalWrite(cs, HIGH);
  g_spi.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE0));
  digitalWrite(cs, LOW);
  g_spi.transfer(0x9F);
  mfg = g_spi.transfer(0x00);
  mem = g_spi.transfer(0x00);
  cap = g_spi.transfer(0x00);
  digitalWrite(cs, HIGH);
  g_spi.endTransaction();
  return !(mfg == 0x00 || mfg == 0xFF);
}

void i2c_scan_collect(JsonArray arr) {
  for (uint8_t addr = 1; addr < 127; ++addr) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      char a[6];
      snprintf(a, sizeof(a), "0x%02X", addr);
      arr.add(a);
    }
  }
}

void ack_to_sue(const PacketView &pkt, bool ok, const char *err = nullptr) {
  StaticJsonDocument<128> resp;
  resp["ok"] = ok;
  if (!ok && err) resp["err"] = err;
  char json[128];
  size_t n = serializeJson(resp, json, sizeof(json));
  PacketHeader hdr = make_hdr(MsgType::ACK, pkt.hdr.src, pkt.hdr.id);
  g_sue_link.sendPacket(hdr, reinterpret_cast<const uint8_t *>(json), n);
}

void exec_command(const PacketView &pkt, const JsonDocument &cmd) {
  const char *c = cmd["cmd"] | "";

  if (strcmp(c, "io_set") == 0) {
    const char *target = cmd["target"] | "";
    int ch = cmd["ch"] | -1;
    int state = cmd["state"] | 0;
    if (strcmp(target, "relay") != 0 || ch < 1 || ch > 4) {
      ack_to_sue(pkt, false, "BAD_REQ");
      return;
    }
    uint8_t pin = (ch == 1) ? IAN_OUT_1 : (ch == 2) ? IAN_OUT_2 : (ch == 3) ? IAN_OUT_3 : IAN_OUT_4;
    digitalWrite(pin, state ? HIGH : LOW);
    ack_to_sue(pkt, true);
    return;
  }

  if (strcmp(c, "estop") == 0) {
    // Local safe: drop outputs
    digitalWrite(IAN_OUT_1, LOW);
    digitalWrite(IAN_OUT_2, LOW);
    digitalWrite(IAN_OUT_3, LOW);
    digitalWrite(IAN_OUT_4, LOW);
    ack_to_sue(pkt, true);
    return;
  }

  ack_to_sue(pkt, false, "UNSUPPORTED");
}

void on_from_sue(const PacketView &pkt, void *) {
  g_sue_linked = true;
  g_last_sue_ms = millis();

  const MsgType t = static_cast<MsgType>(pkt.hdr.type);
  if (t == MsgType::HEARTBEAT) {
    ack_to_sue(pkt, true);
    return;
  }
  if (t != MsgType::COMMAND) return;

  StaticJsonDocument<384> j;
  if (deserializeJson(j, pkt.payload, pkt.hdr.payload_len)) {
    ack_to_sue(pkt, false, "BAD_JSON");
    return;
  }
  exec_command(pkt, j);
}

void on_from_kid(const PacketView &pkt, void *) {
  // Track KIDS by node id from header.src
  uint16_t kid_id = pkt.hdr.src;
  KidInfo *slot = nullptr;
  for (auto &k : g_kids) {
    if (k.id == kid_id || k.id == 0) {
      slot = &k;
      break;
    }
  }
  if (!slot) return;
  if (slot->id == 0) slot->id = kid_id;
  slot->last_ms = millis();

  const MsgType t = static_cast<MsgType>(pkt.hdr.type);
  if (t == MsgType::HEARTBEAT) {
    slot->ready = true;
    // ACK to kid
    StaticJsonDocument<64> a;
    a["ok"] = true;
    char json[64];
    size_t n = serializeJson(a, json, sizeof(json));
    PacketHeader hdr = make_hdr(MsgType::ACK, kid_id, pkt.hdr.id);
    g_kids_link.sendPacket(hdr, reinterpret_cast<const uint8_t *>(json), n);
  }
}

void send_inventory_to_sue() {
  if (!g_sue_linked) return;
  if (millis() - g_last_inventory_ms < 2000) return;
  g_last_inventory_ms = millis();

  StaticJsonDocument<512> inv;
  inv["flash_ok"] = g_flash_ok;
  JsonArray i2c = inv.createNestedArray("i2c");
  i2c_scan_collect(i2c);

  JsonArray kids = inv.createNestedArray("kids");
  for (auto &k : g_kids) {
    if (!k.id) continue;
    JsonObject o = kids.createNestedObject();
    o["id"] = k.id;
    o["ready"] = k.ready;
    o["last_ms"] = (uint32_t)(millis() - k.last_ms);
  }

  char json[512];
  size_t n = serializeJson(inv, json, sizeof(json));
  PacketHeader hdr = make_hdr(MsgType::INVENTORY, SHOWDUINO_NODEID_SUE);
  g_sue_link.sendPacket(hdr, reinterpret_cast<const uint8_t *>(json), n);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(150);
  Serial.println("\n=== Showduino IAN Boot ===");

  // Safe default outputs
  pinMode(IAN_OUT_1, OUTPUT);
  pinMode(IAN_OUT_2, OUTPUT);
  pinMode(IAN_OUT_3, OUTPUT);
  pinMode(IAN_OUT_4, OUTPUT);
  digitalWrite(IAN_OUT_1, LOW);
  digitalWrite(IAN_OUT_2, LOW);
  digitalWrite(IAN_OUT_3, LOW);
  digitalWrite(IAN_OUT_4, LOW);

  // I2C bus
  Wire.begin(IAN_I2C_SDA, IAN_I2C_SCL, IAN_I2C_FREQ_HZ);

  // SPI flash probe
  g_spi.begin(IAN_SPI_SCK, IAN_SPI_MISO, IAN_SPI_MOSI);
  uint8_t m = 0, a = 0, c = 0;
  g_flash_ok = read_jedec(IAN_FLASH_CS, m, a, c);
  Serial.printf("[IAN] W25Qxx: %s (JEDEC %02X %02X %02X)\n", g_flash_ok ? "OK" : "MISSING", m, a, c);

  // Links
  g_sue_link.begin();
  g_kids_link.begin();
  Serial.println("[IAN] UART links up (SUE + KIDS)");
}

void loop() {
  g_sue_link.poll(&on_from_sue, nullptr);
  g_kids_link.poll(&on_from_kid, nullptr);

  if (g_sue_linked && millis() - g_last_sue_ms > 2500) {
    g_sue_linked = false;
    Serial.println("[IAN] SUE heartbeat timeout -> local safe");
    digitalWrite(IAN_OUT_1, LOW);
    digitalWrite(IAN_OUT_2, LOW);
    digitalWrite(IAN_OUT_3, LOW);
    digitalWrite(IAN_OUT_4, LOW);
  }

  send_inventory_to_sue();
  delay(5);
}

