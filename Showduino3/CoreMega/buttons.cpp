#include "buttons.h"

#include <Arduino.h>

#include "config.h"
#include "duoframe.h"

using namespace showduino;

namespace {

struct ButtonState {
  bool stable = false;
  uint32_t lastTransition = 0;
  uint32_t pressStart = 0;
  bool longSent = false;
};

ButtonState states[BUTTON_COUNT];

void selectChannel(uint8_t channel) {
  for (uint8_t i = 0; i < 4; ++i) {
    digitalWrite(MUX_SELECT_PINS[i], (channel >> i) & 0x01);
  }
}

bool readButtonRaw(uint8_t index) {
  uint8_t muxId = index / 16;
  uint8_t channel = index % 16;
  selectChannel(channel);
  delayMicroseconds(2);
  uint8_t sigPin = (muxId == 0) ? MUX_SIG_A : MUX_SIG_B;
  int level = digitalRead(sigPin);
  bool pressed = BUTTON_ACTIVE_LOW ? (level == LOW) : (level == HIGH);
  return pressed;
}

void sendButtonEvent(uint8_t buttonId, uint8_t eventType, uint16_t durationMs) {
  uint8_t payload[4];
  payload[0] = buttonId;
  payload[1] = eventType;
  payload[2] = durationMs >> 8;
  payload[3] = durationMs & 0xFF;
  duoframe::send(DF_CMD_BUTTON_EVENT, payload, sizeof(payload));
}

}  // namespace

void buttons_begin() {
  for (uint8_t pin : MUX_SELECT_PINS) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }

  pinMode(MUX_SIG_A, INPUT_PULLUP);
  pinMode(MUX_SIG_B, INPUT_PULLUP);

  for (auto &state : states) {
    state = ButtonState{};
  }
}

void buttons_update() {
  uint32_t now = millis();

  for (uint8_t i = 0; i < BUTTON_COUNT; ++i) {
    bool reading = readButtonRaw(i);
    ButtonState &state = states[i];

    if (reading != state.stable && (now - state.lastTransition) >= BUTTON_DEBOUNCE_MS) {
      state.stable = reading;
      state.lastTransition = now;

      if (state.stable) {
        state.pressStart = now;
        state.longSent = false;
      } else {
        uint16_t duration = min<uint32_t>(now - state.pressStart, 0xFFFF);
        if (!state.longSent) {
          sendButtonEvent(i, 0, duration);  // short press
        }
      }
    }

    if (state.stable && !state.longSent &&
        (now - state.pressStart) >= BUTTON_LONGPRESS_MS) {
      state.longSent = true;
      sendButtonEvent(i, 1, BUTTON_LONGPRESS_MS);
    }
  }
}

void buttons_applyRelayState(uint8_t, uint8_t) {
  // Placeholder for LED indicators tied to relays
}
