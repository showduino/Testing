#include <Arduino.h>

#include "../common/DuoFrame.h"

#include "addons.h"
#include "audio.h"
#include "buttons.h"
#include "config.h"
#include "dmx.h"
#include "dmx_scene.h"
#include "duoframe.h"
#include "emergency.h"
#include "heartbeat.h"
#include "pixels.h"

using namespace showduino;

namespace {

bool manualMode = true;
uint8_t relayStates[8]{};
uint32_t timelinePosition = 0;
uint32_t lastStatusSent = 0;
bool statusDirty = true;

void markStatusDirty() {
  statusDirty = true;
}

void sendStatusFrame() {
  DuoFrame frame;
  frame.command = DF_CMD_STATUS;
  frame.length = 13;
  frame.payload[0] = manualMode ? 1 : 0;
  memcpy(&frame.payload[1], relayStates, sizeof(relayStates));
  frame.payload[9] = (timelinePosition >> 24) & 0xFF;
  frame.payload[10] = (timelinePosition >> 16) & 0xFF;
  frame.payload[11] = (timelinePosition >> 8) & 0xFF;
  frame.payload[12] = timelinePosition & 0xFF;
  duoframe::sendFrame(frame);
  lastStatusSent = millis();
  statusDirty = false;
}

void handleBrainFrame(const DuoFrame &frame) {
  switch (frame.command) {
    case DF_CMD_HEARTBEAT:
      heartbeat_handleFrame(frame);
      break;

    case DF_CMD_RELAY_SET:
      if (frame.length >= 2 && frame.payload[0] < 8) {
        uint8_t idx = frame.payload[0];
        uint8_t state = frame.payload[1];
        relayStates[idx] = state;
        addons_handleRelay(idx, state);
        buttons_applyRelayState(idx, state);
        markStatusDirty();
      }
      break;

    case DF_CMD_CONTROL_MODE:
      if (frame.length >= 1) {
        manualMode = frame.payload[0];
        markStatusDirty();
      }
      break;

    case DF_CMD_TIMELINE_SEEK:
      if (frame.length >= 5) {
        timelinePosition = ((uint32_t)frame.payload[1] << 24) |
                           ((uint32_t)frame.payload[2] << 16) |
                           ((uint32_t)frame.payload[3] << 8) |
                           ((uint32_t)frame.payload[4]);
        dmx_scene_seek(timelinePosition);
        markStatusDirty();
      }
      break;

    case DF_CMD_AUDIO:
      audio_handleCommand(frame);
      break;

    case DF_CMD_LED_PIXEL:
      dmx_handleCommand(frame);
      break;

    case DF_CMD_EMERGENCY:
      // Brain should not issue this normally, but acknowledge if it does
      duoframe::send(DF_CMD_EMERGENCY, frame.payload, frame.length);
      break;

    default:
      break;
  }
}

void sendStatusIfNeeded() {
  uint32_t now = millis();
  if (statusDirty || (now - lastStatusSent) >= STATUS_INTERVAL_MS) {
    sendStatusFrame();
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n=== Showduino CoreMega Boot ===");

  duoframe::begin(Serial1, DUO_SERIAL_BAUD, DUO_SERIAL_RX, DUO_SERIAL_TX, handleBrainFrame);
  buttons_begin();
  addons_begin();
  audio_begin();
  dmx_begin();
  dmx_scene_begin();
  emergency_begin();
  heartbeat_begin();
  pixels_begin();

  markStatusDirty();
  Serial.println("[CoreMega] Ready");
}

void loop() {
  duoframe::poll();
  buttons_update();
  addons_update();
  emergency_update();
  heartbeat_update();
  audio_update();
  pixels_update();
  sendStatusIfNeeded();
}
