#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

#include "../common/DuoFrame.h"

using namespace showduino;

// ────────────────────────────────────────────────────────────────
//  Hardware wiring
// ────────────────────────────────────────────────────────────────
#define MEGA_RX_PIN 17  // ESP32-S3 RX <- Mega TX
#define MEGA_TX_PIN 18  // ESP32-S3 TX -> Mega RX
#define MEGA_UART Serial1
#define MEGA_BAUD 115200

// Replace with actual MAC addresses during integration
uint8_t UI_PEER_MAC[6] = {0x7C, 0x9E, 0xBD, 0xAA, 0xBB, 0xCC};

// ────────────────────────────────────────────────────────────────
//  State tracking
// ────────────────────────────────────────────────────────────────
struct LinkState {
  bool uiLinked = false;
  bool megaLinked = false;
  uint32_t lastUiHeartbeat = 0;
  uint32_t lastMegaHeartbeat = 0;
} linkState;

// Serial parser state
enum ParserState { WAIT_HEADER, WAIT_LENGTH, WAIT_BODY };
ParserState parserState = WAIT_HEADER;
uint8_t parserBuffer[DUOFRAME_MAX_PAYLOAD + 4];
uint8_t parserExpected = 0;
uint8_t parserIndex = 0;

// ────────────────────────────────────────────────────────────────
//  Forward declarations
// ────────────────────────────────────────────────────────────────
bool sendFrameToUI(const DuoFrame &frame);
bool sendFrameToMega(const DuoFrame &frame);
void handleFrameFromUI(const DuoFrame &frame);
void handleFrameFromMega(const DuoFrame &frame);
void pollMegaSerial();
void sendStatusSnapshot();

// ────────────────────────────────────────────────────────────────
//  ESP-NOW Layer
// ────────────────────────────────────────────────────────────────
void onEspNowSent(const uint8_t *mac, esp_now_send_status_t status) {
  if (status != ESP_NOW_SEND_SUCCESS) {
    Serial.println("[Brain] ESP-NOW send failure");
  }
}

void onEspNowRecv(const uint8_t *mac, const uint8_t *data, int len) {
  DuoFrame frame;
  if (!duoFrameParse(data, len, frame)) {
    Serial.println("[Brain] Invalid DuoFrame from UI");
    return;
  }
  handleFrameFromUI(frame);
}

bool initEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, true);
  if (esp_now_init() != ESP_OK) {
    Serial.println("[Brain] ESP-NOW init failed");
    return false;
  }

  esp_now_register_send_cb(onEspNowSent);
  esp_now_register_recv_cb(onEspNowRecv);

  esp_now_peer_info_t peerInfo{};
  memcpy(peerInfo.peer_addr, UI_PEER_MAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("[Brain] Failed adding UI peer");
    return false;
  }
  return true;
}

bool sendFrameToUI(const DuoFrame &frame) {
  uint8_t buffer[DUOFRAME_MAX_PAYLOAD + 4];
  size_t len = duoFrameSerialize(frame, buffer, sizeof(buffer));
  if (!len) return false;
  return esp_now_send(UI_PEER_MAC, buffer, len) == ESP_OK;
}

bool sendFrameToMega(const DuoFrame &frame) {
  uint8_t buffer[DUOFRAME_MAX_PAYLOAD + 4];
  size_t len = duoFrameSerialize(frame, buffer, sizeof(buffer));
  if (!len) return false;
  return MEGA_UART.write(buffer, len) == len;
}

// ────────────────────────────────────────────────────────────────
//  Frame handlers
// ────────────────────────────────────────────────────────────────
void handleFrameFromUI(const DuoFrame &frame) {
  switch (frame.command) {
    case DF_CMD_HEARTBEAT: {
      linkState.uiLinked = true;
      linkState.lastUiHeartbeat = millis();
      DuoFrame ack;
      ack.command = DF_CMD_HEARTBEAT;
      ack.length = 0;
      sendFrameToUI(ack);
      sendFrameToMega(frame);
      break;
    }

    default:
      sendFrameToMega(frame);
      break;
  }
}

void handleFrameFromMega(const DuoFrame &frame) {
  switch (frame.command) {
    case DF_CMD_HEARTBEAT:
      linkState.megaLinked = true;
      linkState.lastMegaHeartbeat = millis();
      break;

    default:
      break;
  }

  sendFrameToUI(frame);
}

// ────────────────────────────────────────────────────────────────
//  Mega Serial Parser
// ────────────────────────────────────────────────────────────────
void resetParser() {
  parserState = WAIT_HEADER;
  parserIndex = 0;
  parserExpected = 0;
}

void pollMegaSerial() {
  while (MEGA_UART.available()) {
    uint8_t byteIn = MEGA_UART.read();
    switch (parserState) {
      case WAIT_HEADER:
        if (byteIn == DUOFRAME_HEADER) {
          parserBuffer[0] = byteIn;
          parserState = WAIT_LENGTH;
        }
        break;

      case WAIT_LENGTH:
        parserBuffer[1] = byteIn;
        parserExpected = byteIn + 2;  // cmd + payload + checksum
        parserIndex = 0;
        parserState = WAIT_BODY;
        break;

      case WAIT_BODY:
        parserBuffer[2 + parserIndex] = byteIn;
        parserIndex++;
        if (parserIndex >= parserExpected) {
          size_t frameLen = parserBuffer[1] + 4;
          DuoFrame frame;
          if (duoFrameParse(parserBuffer, frameLen, frame)) {
            handleFrameFromMega(frame);
          } else {
            Serial.println("[Brain] Mega frame parse failed");
          }
          resetParser();
        }
        break;
    }
  }
}

// ────────────────────────────────────────────────────────────────
//  Status Snapshot (placeholder)
// ────────────────────────────────────────────────────────────────
void sendStatusSnapshot() {
  static uint32_t lastStatus = 0;
  if (millis() - lastStatus < 1000) return;
  lastStatus = millis();

  // This placeholder status keeps the UI satisfied even before Mega responds.
  DuoFrame frame;
  frame.command = DF_CMD_STATUS;
  frame.length = 13;
  frame.payload[0] = 1;  // manual
  memset(&frame.payload[1], 0, 8);
  uint32_t timeline = (millis() / 10) % 180000;
  frame.payload[9] = (timeline >> 24) & 0xFF;
  frame.payload[10] = (timeline >> 16) & 0xFF;
  frame.payload[11] = (timeline >> 8) & 0xFF;
  frame.payload[12] = timeline & 0xFF;
  sendFrameToUI(frame);
}

// ────────────────────────────────────────────────────────────────
//  Arduino Hooks
// ────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n=== Showduino Brain Boot ===");

  MEGA_UART.begin(MEGA_BAUD, SERIAL_8N1, MEGA_RX_PIN, MEGA_TX_PIN);

  if (!initEspNow()) {
    Serial.println("[Brain] ESP-NOW failure, halting");
    while (true) delay(1000);
  }

  resetParser();
  Serial.println("[Brain] Ready");
}

void loop() {
  pollMegaSerial();
  sendStatusSnapshot();

  if (linkState.uiLinked && millis() - linkState.lastUiHeartbeat > 2500) {
    linkState.uiLinked = false;
    Serial.println("[Brain] UI heartbeat timeout");
  }

  if (linkState.megaLinked && millis() - linkState.lastMegaHeartbeat > 2500) {
    linkState.megaLinked = false;
    Serial.println("[Brain] Mega heartbeat timeout");
  }
}
