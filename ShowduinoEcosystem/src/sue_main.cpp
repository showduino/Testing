#include <Arduino.h>

#include <SPI.h>
#include <SD.h>
#include <Wire.h>

#include <ArduinoJson.h>

#include <showduino/config.h>
#include <showduino/pins_sue.h>
#include <showduino/proto.h>
#include <showduino/link_espnow.h>
#include <showduino/link_uart.h>

using namespace showduino;

namespace {

uint32_t g_seq = 1;

uint8_t g_ui_mac[6]{};
bool g_have_ui_mac = false;
uint32_t g_last_ui_hb_ms = 0;

#if defined(SHOWDUINO_ENABLE_ESPNOW_UI)
EspNowLink g_ui_link;
#endif

#if defined(SHOWDUINO_ENABLE_UART_IAN)
UartLink g_ian_link(Serial1, SUE_IAN_UART_RX, SUE_IAN_UART_TX, SUE_IAN_UART_BAUD);
#endif

SPIClass g_spi(FSPI);

bool g_sd_ok = false;
bool g_rtc_ok = false;
bool g_aht_ok = false;
bool g_flash_ok = false;
uint32_t g_last_status_ms = 0;

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

bool send_to_ui_cobs(const uint8_t *cobs, size_t cobs_len) {
#if !defined(SHOWDUINO_ENABLE_ESPNOW_UI)
  (void)cobs;
  (void)cobs_len;
  return false;
#else
  if (!g_have_ui_mac) return false;
  return g_ui_link.send(g_ui_mac, cobs, cobs_len);
#endif
}

bool send_to_ui_json(MsgType t, uint32_t id_ref, const JsonDocument &doc) {
  char json[256];
  size_t json_len = serializeJson(doc, json, sizeof(json));
  if (!json_len) return false;

  PacketHeader hdr = make_hdr(t, SHOWDUINO_NODEID_UI, id_ref);
  uint8_t cobs[320];
  size_t enc = proto_encode_cobs(hdr, reinterpret_cast<const uint8_t *>(json), json_len, cobs, sizeof(cobs));
  if (!enc) return false;
  return send_to_ui_cobs(cobs, enc);
}

bool probe_i2c_addr(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

void i2c_scan_print() {
  Serial.println("[SUE] I2C scan:");
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; ++addr) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      Serial.printf("  - 0x%02X\n", addr);
      found++;
    }
  }
  if (!found) Serial.println("  (none)");
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

void handle_file_op(const PacketView &pkt, const JsonDocument &req) {
  StaticJsonDocument<256> resp;
  resp["ok"] = false;

  if (!g_sd_ok) {
    resp["err"] = "SD_MISSING";
    send_to_ui_json(MsgType::ACK, pkt.hdr.id, resp);
    return;
  }

  const char *op = req["op"] | "";
  const char *path = req["path"] | "";
  if (!op[0] || !path[0]) {
    resp["err"] = "BAD_REQ";
    send_to_ui_json(MsgType::ACK, pkt.hdr.id, resp);
    return;
  }

  if (strcmp(op, "LIST") == 0) {
    File dir = SD.open(path);
    if (!dir || !dir.isDirectory()) {
      resp["err"] = "NOT_FOUND";
      send_to_ui_json(MsgType::ACK, pkt.hdr.id, resp);
      return;
    }
    JsonArray entries = resp.createNestedArray("entries");
    File f = dir.openNextFile();
    uint8_t count = 0;
    while (f && count < 10) {
      JsonObject e = entries.createNestedObject();
      e["name"] = f.name();
      e["size"] = (uint32_t)f.size();
      e["dir"] = f.isDirectory();
      f = dir.openNextFile();
      count++;
    }
    resp["ok"] = true;
    send_to_ui_json(MsgType::ACK, pkt.hdr.id, resp);
    return;
  }

  resp["err"] = "UNSUPPORTED_OVER_ESPNOW";
  send_to_ui_json(MsgType::ACK, pkt.hdr.id, resp);
}

void handle_from_ui(const uint8_t *mac, const uint8_t *data, int len, void *) {
  memcpy(g_ui_mac, mac, 6);
  g_have_ui_mac = true;
  g_ui_link.addPeer(g_ui_mac);

  uint8_t raw[1200];
  size_t raw_len = proto_decode_cobs(data, (size_t)len, raw, sizeof(raw));
  PacketView pkt{};
  if (!raw_len || !proto_parse_raw(raw, raw_len, pkt)) return;

  const MsgType t = static_cast<MsgType>(pkt.hdr.type);
  if (t == MsgType::HEARTBEAT) {
    g_last_ui_hb_ms = millis();
    StaticJsonDocument<128> resp;
    resp["ok"] = true;
    resp["role"] = "SUE";
    resp["node"] = SHOWDUINO_NODE_ID;
    send_to_ui_json(MsgType::ACK, pkt.hdr.id, resp);
    return;
  }

  StaticJsonDocument<384> req;
  if (deserializeJson(req, pkt.payload, pkt.hdr.payload_len)) {
    StaticJsonDocument<128> nack;
    nack["ok"] = false;
    nack["err"] = "BAD_JSON";
    send_to_ui_json(MsgType::ACK, pkt.hdr.id, nack);
    return;
  }

  if (t == MsgType::FILE_OP) {
    handle_file_op(pkt, req);
    return;
  }

  if (t == MsgType::COMMAND) {
    const char *cmd = req["cmd"] | "";
    if (strcmp(cmd, "estop") == 0) {
      StaticJsonDocument<128> ack;
      ack["ok"] = true;
      ack["armed"] = false;
      send_to_ui_json(MsgType::ACK, pkt.hdr.id, ack);

#if defined(SHOWDUINO_ENABLE_UART_IAN)
      PacketHeader fh = pkt.hdr;
      fh.src = SHOWDUINO_NODE_ID;
      fh.dst = kBroadcast;
      fh.seq = g_seq++;
      fh.ts_ms = millis();
      g_ian_link.sendPacket(fh, pkt.payload, pkt.hdr.payload_len);
#endif
      return;
    }

    StaticJsonDocument<128> ack;
    ack["ok"] = true;
    ack["note"] = "accepted";
    send_to_ui_json(MsgType::ACK, pkt.hdr.id, ack);
  }
}

#if defined(SHOWDUINO_ENABLE_UART_IAN)
void handle_from_ian_uart(const PacketView &pkt, void *) {
  if (!g_have_ui_mac) return;
  uint8_t cobs[320];
  size_t enc = proto_encode_cobs(pkt.hdr, pkt.payload, pkt.hdr.payload_len, cobs, sizeof(cobs));
  if (!enc) return;
  send_to_ui_cobs(cobs, enc);
}
#endif

void send_periodic_status() {
  if (!g_have_ui_mac) return;
  if (millis() - g_last_status_ms < 1000) return;
  g_last_status_ms = millis();

  StaticJsonDocument<256> st;
  st["mode"] = "auto";
  st["sd_ok"] = g_sd_ok;
  st["rtc_ok"] = g_rtc_ok;
  st["aht_ok"] = g_aht_ok;
  st["flash_ok"] = g_flash_ok;
  st["v_raw"] = analogRead(SUE_VOLTAGE_ADC);
  st["ui_link_ms"] = (uint32_t)(millis() - g_last_ui_hb_ms);

  char json[256];
  size_t json_len = serializeJson(st, json, sizeof(json));
  PacketHeader hdr = make_hdr(MsgType::STATUS, SHOWDUINO_NODEID_UI);
  uint8_t cobs[320];
  size_t enc = proto_encode_cobs(hdr, reinterpret_cast<const uint8_t *>(json), json_len, cobs, sizeof(cobs));
  if (enc) send_to_ui_cobs(cobs, enc);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(150);
  Serial.println("\n=== Showduino SUE Boot ===");

  Wire.begin(SUE_I2C_SDA, SUE_I2C_SCL, SUE_I2C_FREQ_HZ);
  i2c_scan_print();
  g_rtc_ok = probe_i2c_addr(0x68);
  g_aht_ok = probe_i2c_addr(0x38);
  Serial.printf("[SUE] RTC: %s | AHT10: %s\n", g_rtc_ok ? "OK" : "MISSING", g_aht_ok ? "OK" : "MISSING");

  g_spi.begin(SUE_SPI_SCK, SUE_SPI_MISO, SUE_SPI_MOSI);
  g_sd_ok = SD.begin(SUE_SD_CS, g_spi);
  Serial.printf("[SUE] SD: %s\n", g_sd_ok ? "OK" : "MISSING");

  uint8_t m = 0, a = 0, c = 0;
  g_flash_ok = read_jedec(SUE_FLASH_CS, m, a, c);
  Serial.printf("[SUE] W25Qxx: %s (JEDEC %02X %02X %02X)\n", g_flash_ok ? "OK" : "MISSING", m, a, c);

  analogReadResolution(12);
  pinMode(SUE_VOLTAGE_ADC, INPUT);
  ledcSetup(0, 2000, 8);
  ledcAttachPin(SUE_MOSFET_PWM, 0);
  ledcWrite(0, 0);

#if defined(SHOWDUINO_ENABLE_UART_IAN)
  g_ian_link.begin();
  Serial.println("[SUE] UART->IAN up");
#endif

#if defined(SHOWDUINO_ENABLE_ESPNOW_UI)
  if (!g_ui_link.begin(&handle_from_ui, nullptr)) {
    Serial.println("[SUE] ESP-NOW init failed");
  } else {
    g_ui_link.addBroadcastPeer();
    Serial.println("[SUE] ESP-NOW up (listening for UI broadcast)");
  }
#endif
}

void loop() {
#if defined(SHOWDUINO_ENABLE_UART_IAN)
  g_ian_link.poll(&handle_from_ian_uart, nullptr);
#endif
  send_periodic_status();
  delay(5);
}

