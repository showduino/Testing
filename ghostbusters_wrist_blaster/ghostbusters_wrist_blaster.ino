/*
  ╔════════════════════════════════════════════════════════════════╗
  ║ GHOSTBUSTERS WRIST BLASTER v4.2 – DFPlayer Mini (Non-Blocking) ║
  ║                                                                ║
  ║ • ESP32 Dev Module                                             ║
  ║ • DFPlayer Mini (DFR0299) on TX=22 RX=0                        ║
  ║ • Hold FIRE to charge, release to blast                        ║
  ║ • Overheat → auto VENT + cooldown                              ║
  ║ • Rumble & NeoPixel effects + Serial HUD                       ║
  ╚════════════════════════════════════════════════════════════════╝
*/

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <DFRobotDFPlayerMini.h>

#define USE_PWM_RUMBLE 0

// ─────────── PIN MAP (your final layout) ───────────
const uint8_t barPins[10] = {2, 4, 12, 13, 14, 15, 16, 17, 32, 33};
#define RING_PIN 23
#define RING_COUNT 12
#define STATUS_PIN 25
#define STATUS_COUNT 2
#define MOSFET_BARREL 26
#define MOSFET_RUMBLE 27
#define BTN_FIRE 18
#define BTN_ACTIVATE 19
#define BTN_MODE 5
#define BTN_SAFETY 21

// ─────────── OBJECTS ───────────
Adafruit_NeoPixel ring(RING_COUNT, RING_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel status(STATUS_COUNT, STATUS_PIN, NEO_GRB + NEO_KHZ800);
DFRobotDFPlayerMini dfplayer;

// ─────────── STATE VARS ───────────
bool active = false, safe = true, isCharging = false, isFiring = false, venting = false, cooling = false;
uint8_t chargeLevel = 0;
uint32_t lastChargeStep = 0, tAct = 0, tSafe = 0, tMode = 0, chargeStart = 0, ventStart = 0, lastHUD = 0;
bool lastAct = HIGH, lastSafe = HIGH, lastMode = HIGH;
const uint32_t DEBOUNCE_MS = 50;

// ─────────── TIMING / MODES ───────────
const uint32_t CHARGE_TIMEOUT = 8000;
uint16_t VENT_DURATION = 3000;
uint16_t COOLDOWN_DURATION = 4000;
enum ChargeMode { RAPID, NORMAL, OVERCHARGE };
ChargeMode chargeMode = NORMAL;
ChargeMode lastChargeMode = NORMAL;
uint16_t CHARGE_TOTAL_MS = 4000;
uint16_t CHARGE_STEP_MS = CHARGE_TOTAL_MS / 10;

// ─────────── SEQUENCE STATE STRUCTS ───────────
struct PowerSequenceState {
  bool active = false;
  bool descending = false;
  uint8_t index = 0;
  uint32_t lastUpdate = 0;
};

struct PowerDownSequenceState {
  bool active = false;
  uint8_t index = 0;
  uint32_t lastUpdate = 0;
};

struct FireSequenceState {
  bool active = false;
  uint8_t remaining = 0;
  uint32_t lastUpdate = 0;
};

struct RingAnimatorState {
  uint8_t offset = 0;
  uint32_t lastFrame = 0;
};

PowerSequenceState powerOnSequenceState;
PowerDownSequenceState powerOffSequenceState;
FireSequenceState fireSequenceState;
RingAnimatorState chargeAnimator;
RingAnimatorState idleAnimator;

// ─────────── HELPERS ───────────
bool edge(uint8_t pin, bool &last, uint32_t &tLast) {
  bool v = digitalRead(pin);
  if (v != last && (millis() - tLast) > DEBOUNCE_MS) {
    last = v;
    tLast = millis();
    return true;
  }
  return false;
}

void setBar(uint8_t lvl) {
  for (uint8_t i = 0; i < 10; i++) {
    digitalWrite(barPins[i], (i < lvl) ? HIGH : LOW);
  }
}

void ringSolid(uint8_t r, uint8_t g, uint8_t b) {
  uint32_t c = ring.Color(r, g, b);
  for (uint8_t i = 0; i < RING_COUNT; i++) {
    ring.setPixelColor(i, c);
  }
  ring.show();
}

void setStatus(uint8_t r, uint8_t g, uint8_t b) {
  status.setPixelColor(0, status.Color(r, g, b));
  status.show();
}

void blinkOrangeIndicator(bool on, uint8_t lvl) {
  static uint32_t t = 0;
  static bool s = false;
  if (!on) {
    status.setPixelColor(1, 0, 0, 0);
    status.show();
    return;
  }
  uint16_t rate = map(lvl, 0, 10, 1000, 150);
  if (millis() - t > rate) {
    t = millis();
    s = !s;
    status.setPixelColor(1, s ? 255 : 0, s ? 80 : 0, 0);
    status.show();
  }
}

// ─────────── NON-BLOCKING AUDIO HELPER ───────────
void playNonBlocking(uint8_t track) {
  static uint32_t lastPlay = 0;
  if (millis() - lastPlay < 300) return;
  lastPlay = millis();
  if (!dfplayer.readState()) dfplayer.play(track);
  Serial.printf("[AUDIO] ▶ Track %d\n", track);
}

// ─────────── POWER SEQUENCES ───────────
void startPowerOnSequence() {
  Serial.println("🔌Power ON");
  playNonBlocking(1);
  powerOffSequenceState.active = false;
  powerOnSequenceState.active = true;
  powerOnSequenceState.descending = false;
  powerOnSequenceState.index = 0;
  powerOnSequenceState.lastUpdate = millis();
  setBar(0);
}

void updatePowerOnSequence(uint32_t now) {
  if (!powerOnSequenceState.active) return;
  uint32_t interval = powerOnSequenceState.descending ? 20 : 40;
  if (now - powerOnSequenceState.lastUpdate < interval) return;
  powerOnSequenceState.lastUpdate = now;

  if (!powerOnSequenceState.descending) {
    digitalWrite(barPins[powerOnSequenceState.index], HIGH);
    if (powerOnSequenceState.index >= 9) {
      powerOnSequenceState.descending = true;
    } else {
      powerOnSequenceState.index++;
    }
  } else {
    digitalWrite(barPins[powerOnSequenceState.index], LOW);
    if (powerOnSequenceState.index == 0) {
      powerOnSequenceState.active = false;
      ringSolid(0, 40, 0);
      setStatus(0, 255, 0);
    } else {
      powerOnSequenceState.index--;
    }
  }
}

void startPowerOffSequence() {
  Serial.println("🔻Power OFF");
  playNonBlocking(5);
  powerOnSequenceState.active = false;
  powerOffSequenceState.active = true;
  powerOffSequenceState.index = 9;
  powerOffSequenceState.lastUpdate = millis();
}

void updatePowerOffSequence(uint32_t now) {
  if (!powerOffSequenceState.active) return;
  if (now - powerOffSequenceState.lastUpdate < 20) return;
  powerOffSequenceState.lastUpdate = now;
  digitalWrite(barPins[powerOffSequenceState.index], LOW);
  if (powerOffSequenceState.index == 0) {
    powerOffSequenceState.active = false;
    ringSolid(0, 0, 0);
    setStatus(0, 0, 0);
    digitalWrite(MOSFET_BARREL, LOW);
    digitalWrite(MOSFET_RUMBLE, LOW);
  } else {
    powerOffSequenceState.index--;
  }
}

// ─────────── FIRE SEQUENCE ───────────
void startFireSequence() {
  if (isFiring || chargeLevel == 0) return;
  isFiring = true;
  fireSequenceState.active = true;
  fireSequenceState.remaining = chargeLevel;
  fireSequenceState.lastUpdate = millis();
  playNonBlocking(3);
  Serial.printf("🔫 FIRE (%d units)\n", chargeLevel);
  digitalWrite(MOSFET_BARREL, HIGH);
  digitalWrite(MOSFET_RUMBLE, HIGH);
  ringSolid(255, 255, 255);
  setStatus(255, 255, 255);
}

void updateFireSequence(uint32_t now) {
  if (!fireSequenceState.active) return;
  if (now - fireSequenceState.lastUpdate < 100) return;
  fireSequenceState.lastUpdate = now;

  if (fireSequenceState.remaining > 0) {
    fireSequenceState.remaining--;
    chargeLevel = fireSequenceState.remaining;
    setBar(chargeLevel);
  }

  if (fireSequenceState.remaining == 0) {
    fireSequenceState.active = false;
    isFiring = false;
    digitalWrite(MOSFET_BARREL, LOW);
    digitalWrite(MOSFET_RUMBLE, LOW);
    ringSolid(0, 0, 0);
    setStatus(0, 0, 255);
    chargeLevel = 0;
    setBar(0);
  }
}

// ─────────── ANIMATION HELPERS ───────────
void updateChargingAnimation(uint32_t now) {
  if (!isCharging || isFiring || venting) return;
  uint8_t lvl = constrain(chargeLevel, 0, 9);
  uint16_t baseDelay = map(lvl, 0, 9, 120, 15);
  if (now - chargeAnimator.lastFrame < baseDelay) return;
  chargeAnimator.lastFrame = now;
  chargeAnimator.offset = (chargeAnimator.offset + 1) % RING_COUNT;
  for (uint8_t i = 0; i < RING_COUNT; i++) {
    uint8_t idx = (i + chargeAnimator.offset) % RING_COUNT;
    uint8_t trail = max(0, 255 - (i * 25));
    ring.setPixelColor(idx, ring.Color(trail, 0, 0));
  }
  ring.show();
}

void updateIdleAnimation(uint32_t now) {
  if (safe || isCharging || isFiring || venting || powerOnSequenceState.active || powerOffSequenceState.active) return;
  if (now - idleAnimator.lastFrame < 80) return;
  idleAnimator.lastFrame = now;
  idleAnimator.offset = (idleAnimator.offset + 1) % RING_COUNT;
  for (uint8_t i = 0; i < RING_COUNT; i++) {
    uint8_t idx = (i + idleAnimator.offset) % RING_COUNT;
    uint8_t trail = max(0, 100 - (i * 10));
    ring.setPixelColor(idx, ring.Color(trail, 0, 0));
  }
  ring.show();
}

// ─────────── MODE HANDLER ───────────
void updateChargeRate(bool force = false) {
  if (!force && chargeMode == lastChargeMode) return;
  lastChargeMode = chargeMode;
  switch (chargeMode) {
    case RAPID:
      CHARGE_TOTAL_MS = 1500;
      setStatus(0, 255, 255);
      Serial.println("⚡Mode:RAPID");
      break;
    case NORMAL:
      CHARGE_TOTAL_MS = 4000;
      setStatus(0, 0, 255);
      Serial.println("⚡Mode:NORMAL");
      break;
    case OVERCHARGE:
      CHARGE_TOTAL_MS = 7000;
      setStatus(255, 0, 255);
      Serial.println("⚡Mode:OVERCHARGE");
      break;
  }
  CHARGE_STEP_MS = CHARGE_TOTAL_MS / 10;
  lastChargeStep = millis();
}

// ─────────── SETUP ───────────
void setup() {
  Serial.begin(115200);
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║ Wrist Blaster v4.2 – DFPlayer Mini ║");
  Serial.println("╚════════════════════════════════════════╝");
  for (uint8_t i = 0; i < 10; i++) {
    pinMode(barPins[i], OUTPUT);
    digitalWrite(barPins[i], LOW);
  }
  pinMode(BTN_FIRE, INPUT_PULLUP);
  pinMode(BTN_ACTIVATE, INPUT_PULLUP);
  pinMode(BTN_MODE, INPUT_PULLUP);
  pinMode(BTN_SAFETY, INPUT_PULLUP);
  pinMode(MOSFET_BARREL, OUTPUT);
  pinMode(MOSFET_RUMBLE, OUTPUT);
  ring.begin();
  ring.setBrightness(120);
  ring.show();
  status.begin();
  status.setBrightness(120);
  status.show();
  // DFPlayer Mini on TX=22 RX=0
  Serial2.begin(9600, SERIAL_8N1, 0, 22);
  if (dfplayer.begin(Serial2)) {
    Serial.println("[AUDIO] ✅ DFPlayer Mini connected");
    dfplayer.volume(25);
  } else {
    Serial.println("[AUDIO] ❌ DFPlayer Mini not detected");
  }
  updateChargeRate(true);
}

// ─────────── MAIN LOOP ───────────
void loop() {
  uint32_t now = millis();

  if (edge(BTN_ACTIVATE, lastAct, tAct)) {
    active = (digitalRead(BTN_ACTIVATE) == LOW);
    if (active) {
      safe = true;
      isCharging = false;
      isFiring = false;
      venting = false;
      cooling = false;
      chargeLevel = 0;
      startPowerOnSequence();
    } else {
      startPowerOffSequence();
    }
  }

  if (edge(BTN_SAFETY, lastSafe, tSafe)) {
    safe = (digitalRead(BTN_SAFETY) == HIGH);
    Serial.printf("Safety → %s\n", safe ? "SAFE" : "ARMED");
  }

  if (edge(BTN_MODE, lastMode, tMode) && digitalRead(BTN_MODE) == LOW) {
    chargeMode = static_cast<ChargeMode>((chargeMode + 1) % 3);
    updateChargeRate(true);
  }

  updatePowerOnSequence(now);
  updatePowerOffSequence(now);
  updateFireSequence(now);

  if (!active) {
    isCharging = false;
    return;
  }

  if (venting) {
    bool flash = ((now / 100) % 2 == 0);
    for (int i = 0; i < 10; i++) {
      digitalWrite(barPins[i], flash ? HIGH : LOW);
    }
    uint8_t fade = static_cast<uint8_t>(abs(static_cast<int>((now / 10) % 510 - 255)));
    ringSolid(fade, fade, fade);
    if (now - ventStart > VENT_DURATION && !cooling) {
      cooling = true;
      ventStart = now;
      Serial.println("💨 Vent complete – cooldown");
      playNonBlocking(4);
      ringSolid(255, 50, 0);
      setBar(0);
      safe = true;
    }
    if (cooling) {
      uint8_t heat = static_cast<uint8_t>(abs(static_cast<int>((now / 8) % 510 - 255)));
      ringSolid(heat / 3, heat / 8, 0);
    }
    if (cooling && now - ventStart > COOLDOWN_DURATION) {
      venting = false;
      cooling = false;
      ventStart = 0;
      safe = false;
      ringSolid(0, 0, 0);
      digitalWrite(MOSFET_RUMBLE, LOW);
      Serial.println("✅ Cooldown finished – re-armed");
    }
    return;
  }

  if (!safe) {
    bool firePressed = (digitalRead(BTN_FIRE) == LOW);
    if (firePressed) {
      if (!isCharging) {
        isCharging = true;
        chargeStart = now;
        lastChargeStep = now;
        chargeAnimator.lastFrame = now;
        playNonBlocking(2);
      }
    } else if (isCharging) {
      isCharging = false;
      chargeStart = 0;
      if (chargeLevel > 0) {
        startFireSequence();
      } else {
        ringSolid(0, 0, 0);
        setStatus(0, 0, 255);
      }
    }

    if (isCharging) {
      updateChargingAnimation(now);
      if (chargeLevel < 10 && now - lastChargeStep >= CHARGE_STEP_MS) {
        chargeLevel++;
        setBar(chargeLevel);
        lastChargeStep = now;
      }
      if (chargeStart > 0 && now - chargeStart > CHARGE_TIMEOUT) {
        Serial.println("🔥 OVERHEAT → VENT");
        playNonBlocking(4);
        venting = true;
        cooling = false;
        isCharging = false;
        chargeStart = 0;
        chargeLevel = 0;
        setBar(10);
        ventStart = now;
        digitalWrite(MOSFET_BARREL, LOW);
        digitalWrite(MOSFET_RUMBLE, HIGH);
        ringSolid(255, 255, 255);
        return;
      }
    }
  } else {
    isCharging = false;
  }

  updateChargingAnimation(now);
  updateIdleAnimation(now);

  bool idleState = (!safe && !isCharging && !isFiring && chargeLevel == 0);
  bool blinkActive = (safe || isCharging || chargeLevel >= 10 || idleState);
  uint8_t blinkLevel = idleState ? 3 : (chargeLevel >= 10 ? 10 : chargeLevel);
  blinkOrangeIndicator(blinkActive, blinkLevel);

  if (now - lastHUD > 250) {
    lastHUD = now;
    Serial.printf("[HUD] ACT=%d SAFE=%d FIRE=%d | active=%d safe=%d charge=%d vent=%d cool=%d\n",
                  digitalRead(BTN_ACTIVATE), digitalRead(BTN_SAFETY), digitalRead(BTN_FIRE),
                  active, safe, chargeLevel, venting, cooling);
  }
}
