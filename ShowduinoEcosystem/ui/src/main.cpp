#include <Arduino.h>

#include <ArduinoJson.h>

#include <showduino/config.h>
#include <showduino/pins_ui.h>
#include <showduino/proto.h>
#include <showduino/link_espnow.h>

using namespace showduino;

namespace {

uint32_t g_seq = 1;
uint32_t g_last_sue_seen_ms = 0;
uint8_t g_last_sue_mac[6]{};
bool g_have_sue_mac = false;

#if defined(SHOWDUINO_ENABLE_ESPNOW_SUE)
EspNowLink g_link;
#endif

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

bool send_packet_broadcast(MsgType t, const JsonDocument &doc, uint16_t dst = SHOWDUINO_NODEID_SUE) {
#if !defined(SHOWDUINO_ENABLE_ESPNOW_SUE)
  (void)t;
  (void)doc;
  (void)dst;
  return false;
#else
  char json[200];
  size_t json_len = serializeJson(doc, json, sizeof(json));
  if (!json_len) return false;
  PacketHeader hdr = make_hdr(t, dst);
  uint8_t cobs[240];
  size_t enc = proto_encode_cobs(hdr, reinterpret_cast<const uint8_t *>(json), json_len, cobs, sizeof(cobs));
  if (!enc) return false;
  const uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  return g_link.send(bcast, cobs, enc);
#endif
}

bool send_packet_to_sue(MsgType t, const JsonDocument &doc, uint16_t dst = SHOWDUINO_NODEID_SUE) {
  if (!g_have_sue_mac) return send_packet_broadcast(t, doc, dst);
#if !defined(SHOWDUINO_ENABLE_ESPNOW_SUE)
  return false;
#else
  char json[200];
  size_t json_len = serializeJson(doc, json, sizeof(json));
  if (!json_len) return false;
  PacketHeader hdr = make_hdr(t, dst);
  uint8_t cobs[240];
  size_t enc = proto_encode_cobs(hdr, reinterpret_cast<const uint8_t *>(json), json_len, cobs, sizeof(cobs));
  if (!enc) return false;
  return g_link.send(g_last_sue_mac, cobs, enc);
#endif
}

void on_recv(const uint8_t *mac, const uint8_t *data, int len, void *) {
  // Decode packet
  uint8_t raw[600];
  size_t raw_len = proto_decode_cobs(data, (size_t)len, raw, sizeof(raw));
  PacketView pkt{};
  if (!raw_len || !proto_parse_raw(raw, raw_len, pkt)) return;

  // If it's from SUE, capture MAC for directed sends.
  if (pkt.hdr.src == SHOWDUINO_NODEID_SUE) {
    memcpy(g_last_sue_mac, mac, 6);
    g_have_sue_mac = true;
    g_last_sue_seen_ms = millis();
  }

  StaticJsonDocument<256> j;
  DeserializationError err = deserializeJson(j, pkt.payload, pkt.hdr.payload_len);
  if (err) return;

  const MsgType t = static_cast<MsgType>(pkt.hdr.type);
  if (t == MsgType::STATUS) {
    Serial.print("[UI] STATUS: ");
    serializeJson(j, Serial);
    Serial.println();
  } else if (t == MsgType::ACK) {
    Serial.print("[UI] ACK: ");
    serializeJson(j, Serial);
    Serial.println();
  }
}

void send_heartbeat() {
  StaticJsonDocument<64> hb;
  hb["role"] = "UI";
  hb["node"] = SHOWDUINO_NODE_ID;
  send_packet_broadcast(MsgType::HEARTBEAT, hb);
}

}  // namespace

void setup() {
  // UI module uses touch on GPIO19/20, so don't rely on native USB.
  Serial.begin(115200);
  delay(150);
  Serial.println("\n=== Showduino UI Boot (minimal) ===");

#if defined(SHOWDUINO_ENABLE_ESPNOW_SUE)
  if (!g_link.begin(&on_recv, nullptr)) {
    Serial.println("[UI] ESP-NOW init failed");
  } else {
    g_link.addBroadcastPeer();
    Serial.println("[UI] ESP-NOW up (broadcast discovery)");
  }
#endif
}

void loop() {
  static uint32_t last_hb = 0;
  if (millis() - last_hb > 1000) {
    send_heartbeat();
    last_hb = millis();
  }

  // Demo: request SD root listing occasionally (small response)
  static uint32_t last_list = 0;
  if (millis() - last_list > 5000) {
    StaticJsonDocument<96> req;
    req["op"] = "LIST";
    req["path"] = "/";
    send_packet_to_sue(MsgType::FILE_OP, req);
    last_list = millis();
  }

  if (g_have_sue_mac && millis() - g_last_sue_seen_ms > 3000) {
    g_have_sue_mac = false;
    Serial.println("[UI] SUE link timeout (falling back to broadcast)");
  }

  delay(5);
}

