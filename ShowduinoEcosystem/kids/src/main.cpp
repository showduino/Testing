#include <Arduino.h>

#include <ArduinoJson.h>

#include <showduino/config.h>
#include <showduino/pins_kid.h>
#include <showduino/proto.h>
#include <showduino/link_uart.h>

using namespace showduino;

namespace {

uint32_t g_seq = 1;
uint32_t g_last_parent_ms = 0;
bool g_parent_linked = false;

HardwareSerial ParentSerial(1);
UartLink g_parent(ParentSerial, KID_PARENT_UART_RX, KID_PARENT_UART_TX, KID_PARENT_UART_BAUD);

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

void on_from_parent(const PacketView &pkt, void *) {
  g_parent_linked = true;
  g_last_parent_ms = millis();

  const MsgType t = static_cast<MsgType>(pkt.hdr.type);
  if (t == MsgType::ACK) return;

  // Minimal command support (future expands per KID role)
  if (t == MsgType::COMMAND) {
    StaticJsonDocument<192> j;
    if (deserializeJson(j, pkt.payload, pkt.hdr.payload_len)) return;
    const char *cmd = j["cmd"] | "";
    if (strcmp(cmd, "kid_led") == 0) {
      int on = j["on"] | 0;
      digitalWrite(KID_STATUS_LED, on ? HIGH : LOW);
    }
    // ACK
    StaticJsonDocument<64> a;
    a["ok"] = true;
    char json[64];
    size_t n = serializeJson(a, json, sizeof(json));
    PacketHeader hdr = make_hdr(MsgType::ACK, pkt.hdr.src, pkt.hdr.id);
    g_parent.sendPacket(hdr, reinterpret_cast<const uint8_t *>(json), n);
  }
}

void send_heartbeat() {
  StaticJsonDocument<128> hb;
  hb["ready"] = true;
  hb["fw"] = "0.1.0";
  hb["s1"] = analogRead(KID_SENSOR_1);
  hb["s2"] = analogRead(KID_SENSOR_2);
  hb["led"] = (int)digitalRead(KID_STATUS_LED);

  char json[160];
  size_t n = serializeJson(hb, json, sizeof(json));
  PacketHeader hdr = make_hdr(MsgType::HEARTBEAT, kBroadcast);
  g_parent.sendPacket(hdr, reinterpret_cast<const uint8_t *>(json), n);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(150);
  Serial.println("\n=== Showduino KIDS Boot ===");

  pinMode(KID_STATUS_LED, OUTPUT);
  digitalWrite(KID_STATUS_LED, LOW);
  pinMode(KID_SENSOR_1, INPUT);
  pinMode(KID_SENSOR_2, INPUT);

  analogReadResolution(12);

  g_parent.begin();
  Serial.println("[KID] UART parent link up");
}

void loop() {
  g_parent.poll(&on_from_parent, nullptr);

  static uint32_t last_hb = 0;
  if (millis() - last_hb > 500) {
    send_heartbeat();
    last_hb = millis();
  }

  if (g_parent_linked && millis() - g_last_parent_ms > 2500) {
    g_parent_linked = false;
    digitalWrite(KID_STATUS_LED, LOW);
    Serial.println("[KID] parent timeout -> safe");
  }

  delay(5);
}

