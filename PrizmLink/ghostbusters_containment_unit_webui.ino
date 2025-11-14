/*
 * ESP32 Containment Unit - Build 1.1
 * Adds Wi-Fi + AsyncWebServer-based WebUI dashboard
 * for real-time monitoring of the containment workflow.
 *
 * Hardware assumptions:
 *  - ESP32 (tested on ESP32-S3 dev modules)
 *  - Input switches wired with pull-ups (active LOW)
 *  - Relay modules are active LOW
 *
 * Dependencies (install via Arduino Library Manager or PlatformIO):
 *  - ESP Async WebServer (https://github.com/me-no-dev/ESPAsyncWebServer)
 *  - AsyncTCP (ESP32)
 *  - ArduinoJson
 */

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <SerialMP3Player.h>

// ===== PIN DEFINITIONS =====
// Input Pins
const int POWER_BUTTON_PIN = 13;      // Power Button
const int LIMIT_SWITCH_PIN = 34;      // Door position sensor
const int GREEN_SWITCH_PIN = 32;      // Ready Switch
const int RED_SWITCH_PIN = 33;        // Containment Lock
const int ORANGE_SWITCH_PIN = 4;      // Final Lock
const int LEVER_SWITCH_PIN = 15;      // Final Step Lever

// Ultrasonic Sensor Pins
const int TRIG_PIN = 5;               // Ultrasonic Trigger
const int ECHO_PIN = 18;              // Ultrasonic Echo

// LED Pins
const int GREEN_LED_PIN = 22;         // System Ready Indicator
const int RED_LED_PIN = 23;           // Containment Warning

// Audio / Buzzer Pins
const int MP3_RX_PIN = 16;            // ESP32 RX (connect to MP3 TX)
const int MP3_TX_PIN = 17;            // ESP32 TX (connect to MP3 RX)
const int RADAR_BUZZER_PIN = 14;      // Piezo/buzzer for proximity tone
const int RADAR_BUZZER_CHANNEL = 6;

// Relay Pins
const int RELAY_GREEN_PIN = 19;       // Green Switch Relay
const int RELAY_RED_PIN = 21;         // Red Switch Relay
const int RELAY_ORANGE_PIN = 25;      // Orange Switch Relay
const int RELAY_SMOKE_PIN = 26;       // Smoke Effect Relay
const int RELAY_LOCK_PIN = 27;        // Containment Lock Relay

// ===== CONFIGURATION CONSTANTS =====
const int TRAP_DISTANCE_THRESHOLD_CM = 20;          // Distance to detect trap insertion
const int ULTRASONIC_TIMEOUT_US = 30000;            // Ultrasonic sensor timeout
const unsigned long SMOKE_DURATION_MS = 2000;       // How long smoke effect runs
const unsigned long DEBOUNCE_DELAY_MS = 50;         // Button debounce time
const unsigned long SENSOR_READ_INTERVAL_MS = 100;  // How often to read ultrasonic
const unsigned long SERIAL_BAUD_RATE = 115200;
const unsigned long IDLE_LOOP_INTERVAL_MS = 8000;

// Relay States (Active Low for most relay modules)
const int RELAY_ON = LOW;
const int RELAY_OFF = HIGH;

// ===== WI-FI / WEB CONFIGURATION =====
const char *WIFI_STA_SSID = "ContainmentLab";      // TODO: update with your SSID
const char *WIFI_STA_PASSWORD = "ghostmode123";    // TODO: update with your password
const char *WIFI_AP_SSID = "Containment-Unit";
const char *WIFI_AP_PASSWORD = "gozer1984";
const char *WIFI_HOSTNAME = "containment-unit";
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 12000;

AsyncWebServer webServer(80);
bool wifiConnected = false;
bool wifiApMode = false;
SerialMP3Player mp3(MP3_RX_PIN, MP3_TX_PIN);
bool audioReady = false;
unsigned long lastIdleLoopMs = 0;
bool radarToneActive = false;

// MP3 track assignments
constexpr uint8_t kTrackButtonOrange = 1;
constexpr uint8_t kTrackButtonRed = 2;
constexpr uint8_t kTrackEPA = 3;
constexpr uint8_t kTrackStartup = 4;
constexpr uint8_t kTrackPowerDown = 5;
constexpr uint8_t kTrackDoorOpen = 7;
constexpr uint8_t kTrackTrapInsert = 8;
constexpr uint8_t kTrackIdleLoop = 9;
constexpr uint8_t kTrackDoorClose = 10;
constexpr uint8_t kTrackTrapClean = 11;
constexpr uint8_t kTrackGreenButton = 6;

// ===== SYSTEM STATES =====
enum SystemState {
  STATE_OFF,                    // System powered off
  STATE_READY,                  // System on, waiting for door open
  STATE_DOOR_OPEN,              // Door open, waiting for trap
  STATE_TRAP_DETECTED,          // Trap detected, waiting for door close
  STATE_DOOR_CLOSED,            // Door closed, waiting for red button
  STATE_RED_PRESSED,            // Red pressed, waiting for orange button
  STATE_ORANGE_PRESSED,         // Orange pressed, waiting for lever down
  STATE_LEVER_DOWN,             // Lever down, smoke activated
  STATE_PROCESSING,             // Processing complete, waiting for lever up
  STATE_ERROR                   // Error state
};

// ===== GLOBAL VARIABLES =====
SystemState currentState = STATE_OFF;
SystemState previousState = STATE_OFF;

// Timing variables for non-blocking operations
unsigned long lastSensorRead = 0;
unsigned long smokeStartTime = 0;

// Button state tracking for debouncing
struct ButtonState {
  bool currentState = HIGH;
  bool lastState = HIGH;
  bool stableState = HIGH;
  unsigned long lastChangeTime = 0;
};

ButtonState powerButton, greenSwitch, redSwitch, orangeSwitch, leverSwitch, limitSwitch;

// Sensor data
int currentDistance = -1;
bool trapDetected = false;

// ===== WEB UI TEMPLATE =====
const char CONTAINMENT_UI_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <meta name="color-scheme" content="dark" />
  <title>Containment Unit Console</title>
  <style>
    :root {
      --bg: #050505;
      --panel: #111;
      --accent: #e0003c;
      --accent-soft: #ff8c00;
      --text: #f5f5f5;
      --muted: #888;
      --success: #37ff8b;
      --warning: #ffc400;
      --danger: #ff4757;
      --card: rgba(255,255,255,0.04);
      --border: rgba(255,255,255,0.12);
      font-family: 'Montserrat', 'Segoe UI', sans-serif;
    }
    * { box-sizing: border-box; }
    body {
      background: radial-gradient(circle at top, rgba(224,0,60,0.18), transparent 55%), var(--bg);
      color: var(--text);
      margin: 0;
      min-height: 100vh;
      display: flex;
      flex-direction: column;
    }
    header {
      padding: 1.5rem 1rem;
      text-align: center;
      border-bottom: 1px solid var(--border);
    }
    header h1 {
      margin: 0;
      font-size: 1.7rem;
      letter-spacing: 0.08em;
    }
    header p {
      margin: 0.3rem 0 0;
      color: var(--muted);
      font-size: 0.9rem;
    }
    main {
      flex: 1;
      width: 100%;
      max-width: 1100px;
      margin: 0 auto;
      padding: 1.5rem;
    }
    .grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
      gap: 1rem;
    }
    .card {
      background: var(--card);
      border: 1px solid var(--border);
      border-radius: 14px;
      padding: 1rem 1.2rem;
      backdrop-filter: blur(6px);
      box-shadow: 0 8px 20px rgba(0,0,0,0.35);
      transition: transform 0.2s ease;
    }
    .card:hover { transform: translateY(-4px); }
    .card h2 {
      margin: 0;
      font-size: 0.95rem;
      text-transform: uppercase;
      letter-spacing: 0.1em;
      color: var(--muted);
    }
    .metric {
      margin-top: 0.6rem;
      font-size: 2rem;
      font-weight: 600;
      letter-spacing: 0.05em;
    }
    .metric small {
      display: block;
      font-size: 0.8rem;
      color: var(--muted);
      margin-top: 0.35rem;
    }
    .badge {
      display: inline-flex;
      align-items: center;
      padding: 0.25rem 0.6rem;
      border-radius: 999px;
      font-size: 0.75rem;
      letter-spacing: 0.1em;
      margin-top: 0.4rem;
    }
    .badge.on { background: rgba(55, 255, 139, 0.12); color: var(--success); border: 1px solid rgba(55,255,139,0.4); }
    .badge.off { background: rgba(255, 71, 87, 0.12); color: var(--danger); border: 1px solid rgba(255,71,87,0.4); }
    .badge.warning { background: rgba(255,196,0,0.12); color: var(--warning); border: 1px solid rgba(255,196,0,0.4); }
    .status-panel {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
      gap: 1rem;
      margin-top: 1.5rem;
    }
    .status-item {
      background: var(--card);
      border: 1px solid var(--border);
      border-radius: 12px;
      padding: 0.9rem 1.1rem;
    }
    .status-item h3 {
      margin: 0;
      font-size: 0.85rem;
      text-transform: uppercase;
      letter-spacing: 0.08em;
      color: var(--muted);
    }
    .status-item span {
      display: block;
      margin-top: 0.35rem;
      font-size: 1.3rem;
      font-weight: 600;
    }
    .checklist {
      margin-top: 2rem;
      padding: 1.2rem;
      border-radius: 16px;
      background: linear-gradient(135deg, rgba(224,0,60,0.18), rgba(255,140,0,0.18));
      border: 1px solid rgba(255,255,255,0.08);
      box-shadow: 0 10px 18px rgba(0,0,0,0.35);
    }
    .checklist h2 {
      margin: 0 0 1rem 0;
      text-transform: uppercase;
      letter-spacing: 0.12em;
      font-size: 1rem;
    }
    .checklist ul {
      list-style: none;
      padding: 0;
      margin: 0;
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
      gap: 0.6rem;
    }
    .checklist li {
      padding: 0.7rem 0.8rem;
      border-radius: 10px;
      border: 1px solid rgba(255,255,255,0.2);
      background: rgba(0,0,0,0.25);
      font-size: 0.85rem;
      display: flex;
      align-items: center;
      gap: 0.4rem;
    }
    .checklist li.active {
      border-color: var(--accent);
      background: rgba(224,0,60,0.2);
    }
    .checklist li.done {
      border-color: var(--success);
      background: rgba(55,255,139,0.18);
    }
    .connection {
      margin-top: 1rem;
      text-align: center;
      font-size: 0.9rem;
      color: var(--muted);
    }
    .connection span {
      font-weight: 600;
      color: var(--text);
    }
    footer {
      text-align: center;
      padding: 1rem;
      font-size: 0.8rem;
      color: var(--muted);
      border-top: 1px solid var(--border);
    }
    @media (max-width: 640px) {
      main { padding: 1rem; }
      header h1 { font-size: 1.3rem; }
    }
  </style>
</head>
<body>
  <header>
    <h1>Containment Unit Console</h1>
    <p>Ghostbusters Facility – Remote Supervisor View</p>
    <div class="connection">Link: <span id="linkState">offline</span> • IP: <span id="ipAddr">0.0.0.0</span></div>
  </header>
  <main>
    <section class="grid">
      <article class="card">
        <h2>System State</h2>
        <div class="metric" id="stateValue">OFF</div>
        <small id="stateNote">Awaiting power</small>
      </article>
      <article class="card">
        <h2>Door Status</h2>
        <div class="metric" id="doorValue">Closed</div>
        <div id="doorBadge" class="badge off">SEALED</div>
      </article>
      <article class="card">
        <h2>Trap Detection</h2>
        <div class="metric" id="trapValue">No Trap</div>
        <small id="distanceValue">Distance: -- cm</small>
        <div id="trapBadge" class="badge off">IDLE</div>
      </article>
      <article class="card">
        <h2>Smoke / Lock</h2>
        <div class="metric" id="smokeValue">Standby</div>
        <small id="lockValue">Lock: disengaged</small>
        <div id="smokeBadge" class="badge off">SMOKE</div>
      </article>
    </section>

    <section class="status-panel">
      <div class="status-item">
        <h3>Wi-Fi Mode</h3>
        <span id="wifiMode">STA</span>
        <small id="wifiSsid"></small>
      </div>
      <div class="status-item">
        <h3>Last Event</h3>
        <span id="lastEvent">None</span>
      </div>
      <div class="status-item">
        <h3>Uptime</h3>
        <span id="uptime">0s</span>
      </div>
      <div class="status-item">
        <h3>Sequence Index</h3>
        <span id="sequenceIndex">0 / 8</span>
      </div>
    </section>

    <section class="checklist">
      <h2>Sequence Checklist</h2>
      <ul id="sequenceList">
        <li data-state="READY">Power / Ready</li>
        <li data-state="DOOR_OPEN">Door Open</li>
        <li data-state="TRAP_DETECTED">Trap Detected</li>
        <li data-state="DOOR_CLOSED">Door Closed</li>
        <li data-state="RED_PRESSED">Red Button</li>
        <li data-state="ORANGE_PRESSED">Orange Button</li>
        <li data-state="LEVER_DOWN">Lever Down</li>
        <li data-state="PROCESSING">Processing</li>
      </ul>
    </section>
  </main>
  <footer>Live telemetry updates every second • Built for ESP32 Containment Unit</footer>
  <script>
    const stateValue = document.getElementById('stateValue');
    const stateNote = document.getElementById('stateNote');
    const doorValue = document.getElementById('doorValue');
    const doorBadge = document.getElementById('doorBadge');
    const trapValue = document.getElementById('trapValue');
    const trapBadge = document.getElementById('trapBadge');
    const distanceValue = document.getElementById('distanceValue');
    const smokeValue = document.getElementById('smokeValue');
    const smokeBadge = document.getElementById('smokeBadge');
    const lockValue = document.getElementById('lockValue');
    const wifiMode = document.getElementById('wifiMode');
    const wifiSsid = document.getElementById('wifiSsid');
    const linkState = document.getElementById('linkState');
    const ipAddr = document.getElementById('ipAddr');
    const lastEvent = document.getElementById('lastEvent');
    const uptimeEl = document.getElementById('uptime');
    const sequenceIndex = document.getElementById('sequenceIndex');
    const sequenceList = document.getElementById('sequenceList').children;

    const notes = {
      OFF: "System is idle",
      READY: "Waiting for door to open",
      DOOR_OPEN: "Insert trap to continue",
      TRAP_DETECTED: "Close door to lock trap",
      DOOR_CLOSED: "Press the red button",
      RED_PRESSED: "Awaiting orange confirmation",
      ORANGE_PRESSED: "Throw the containment lever",
      LEVER_DOWN: "Smoke active – venting energy",
      PROCESSING: "Return lever to reset",
      ERROR: "Power cycle to recover"
    };

    function setBadge(el, state) {
      el.classList.remove('on', 'off', 'warning');
      if (state === 'on') el.classList.add('on');
      else if (state === 'warning') el.classList.add('warning');
      else el.classList.add('off');
    }

    function updateSequence(currentState) {
      const order = ["READY","DOOR_OPEN","TRAP_DETECTED","DOOR_CLOSED","RED_PRESSED","ORANGE_PRESSED","LEVER_DOWN","PROCESSING"];
      let idx = order.indexOf(currentState);
      if (idx < 0) idx = -1;
      for (let i = 0; i < sequenceList.length; i++) {
        const item = sequenceList[i];
        item.classList.remove('active','done');
        if (idx === -1) continue;
        if (i < idx) item.classList.add('done');
        if (i === idx) item.classList.add('active');
      }
      sequenceIndex.textContent = `${Math.max(idx, 0) + 1} / ${order.length}`;
    }

    async function pollStatus() {
      try {
        const res = await fetch('/status');
        if (!res.ok) throw new Error('HTTP ' + res.status);
        const data = await res.json();
        linkState.textContent = 'online';
        ipAddr.textContent = data.wifi?.ip || '0.0.0.0';
        stateValue.textContent = data.state || 'UNKNOWN';
        stateNote.textContent = notes[data.state] || 'Monitoring';
        doorValue.textContent = data.doorOpen ? 'OPEN' : 'CLOSED';
        setBadge(doorBadge, data.doorOpen ? 'warning' : 'off');
        doorBadge.textContent = data.doorOpen ? 'UNSEALED' : 'SEALED';
        trapValue.textContent = data.trapDetected ? 'Trap Ready' : 'No Trap';
        distanceValue.textContent = `Distance: ${data.distanceCm >= 0 ? data.distanceCm : '--'} cm`;
        setBadge(trapBadge, data.trapDetected ? 'on' : 'off');
        trapBadge.textContent = data.trapDetected ? 'CAPTURE' : 'IDLE';
        smokeValue.textContent = data.smokeActive ? 'VENTING' : 'Standby';
        setBadge(smokeBadge, data.smokeActive ? 'warning' : 'off');
        smokeBadge.textContent = data.smokeActive ? 'ACTIVE' : 'SMOKE';
        lockValue.textContent = `Lock: ${data.lockEngaged ? 'engaged' : 'disengaged'}`;
        wifiMode.textContent = data.wifi?.mode || 'STA';
        wifiSsid.textContent = data.wifi?.ssid || '';
        lastEvent.textContent = data.lastEvent || 'N/A';
        uptimeEl.textContent = `${Math.round((data.timestamp || 0) / 1000)}s`;
        updateSequence(data.state || 'READY');
      } catch (err) {
        linkState.textContent = 'offline';
        console.warn('Status poll failed', err);
      }
    }

    pollStatus();
    setInterval(pollStatus, 1000);
  </script>
</body>
</html>
)HTML";

// ===== FUNCTION DECLARATIONS =====
void setupPins();
void setupWiFi();
void setupWebServer();
void updateButtonStates();
void updateButtonState(ButtonState &button, int pin);
bool isButtonPressed(const ButtonState &button);
bool isButtonJustPressed(ButtonState &button);
bool isButtonJustReleased(ButtonState &button);
void updateSensors();
int measureDistance();
void handleStateMachine();
SystemState handleStateOff();
SystemState handleStateReady();
SystemState handleStateDoorOpen();
SystemState handleStateTrapDetected();
SystemState handleStateDoorClosed();
SystemState handleStateRedPressed();
SystemState handleStateOrangePressed();
SystemState handleStateLeverDown();
SystemState handleStateProcessing();
SystemState handleStateError();
void changeState(SystemState newState);
void onStateEnter(SystemState state);
void updateOutputs();
void updateLEDs();
void updateRelays();
String getStateName(SystemState state);
void resetSystem();
void populateStatusJson(JsonDocument &doc);
void respondWithStatus(AsyncWebServerRequest *request);
String currentIpString();
void initAudio();
void playTrack(uint8_t track, const char *label = nullptr);
void updateIdleAudio();
void updateRadarAudio();
void stopRadarTone();

// ===== SETUP FUNCTION =====
void setup() {
  Serial.begin(SERIAL_BAUD_RATE);
  delay(200);
  Serial.println("🚀 ESP32 Containment Unit with WebUI Starting...");

  setupPins();
  resetSystem();
  initAudio();
  setupWiFi();
  setupWebServer();

  Serial.println("✅ System Initialized - Press Power Button or use manual controls");
}

// ===== MAIN LOOP =====
void loop() {
  updateButtonStates();
  updateSensors();
  handleStateMachine();
  updateOutputs();
  updateIdleAudio();

  // Small delay to prevent excessive CPU usage
  delay(10);
}

// ===== PIN SETUP =====
void setupPins() {
  // Setup input pins with internal pull-ups
  pinMode(POWER_BUTTON_PIN, INPUT_PULLUP);
  pinMode(GREEN_SWITCH_PIN, INPUT_PULLUP);
  pinMode(RED_SWITCH_PIN, INPUT_PULLUP);
  pinMode(ORANGE_SWITCH_PIN, INPUT_PULLUP);
  pinMode(LEVER_SWITCH_PIN, INPUT_PULLUP);
  pinMode(LIMIT_SWITCH_PIN, INPUT_PULLUP);

  // Setup ultrasonic sensor pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  // Setup LED pins
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);

  // Setup relay pins
  pinMode(RELAY_GREEN_PIN, OUTPUT);
  pinMode(RELAY_RED_PIN, OUTPUT);
  pinMode(RELAY_ORANGE_PIN, OUTPUT);
  pinMode(RELAY_SMOKE_PIN, OUTPUT);
  pinMode(RELAY_LOCK_PIN, OUTPUT);

  // Radar buzzer (LEDC tone output)
  ledcSetup(RADAR_BUZZER_CHANNEL, 1000, 10);
  ledcAttachPin(RADAR_BUZZER_PIN, RADAR_BUZZER_CHANNEL);
  stopRadarTone();

  Serial.println("📌 Pins configured successfully");
}

// ===== WI-FI & WEB SERVER =====
void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(WIFI_HOSTNAME);
  WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASSWORD);
  Serial.printf("📡 Connecting to %s ...\n", WIFI_STA_SSID);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    wifiApMode = false;
    Serial.printf("✅ Wi-Fi connected: %s (RSSI %d dBm)\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    Serial.println("⚠️ Wi-Fi STA failed. Enabling fallback AP mode.");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
    wifiApMode = true;
    wifiConnected = false;
    Serial.printf("✅ SoftAP active: %s (%s)\n", WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());
  }
}

void setupWebServer() {
  webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", CONTAINMENT_UI_HTML);
  });

  webServer.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    respondWithStatus(request);
  });

  webServer.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "application/json", "{\"error\":\"Not found\"}");
  });

  webServer.begin();
  Serial.println("🌐 Web server online at http://" + currentIpString());
}

String currentIpString() {
  if (wifiApMode) {
    return WiFi.softAPIP().toString();
  }
  if (WiFi.status() == WL_CONNECTED) {
    return WiFi.localIP().toString();
  }
  return "0.0.0.0";
}

void respondWithStatus(AsyncWebServerRequest *request) {
  StaticJsonDocument<512> doc;
  populateStatusJson(doc);

  String json;
  serializeJson(doc, json);
  request->send(200, "application/json", json);
}

void populateStatusJson(JsonDocument &doc) {
  doc["state"] = getStateName(currentState);
  doc["previousState"] = getStateName(previousState);
  doc["doorOpen"] = !isButtonPressed(limitSwitch);
  doc["trapDetected"] = trapDetected;
  doc["distanceCm"] = currentDistance;
  doc["smokeActive"] = (currentState == STATE_LEVER_DOWN);
  doc["lockEngaged"] = (currentState >= STATE_DOOR_CLOSED && currentState <= STATE_PROCESSING);
  doc["timestamp"] = millis();
  doc["lastEvent"] = getStateName(previousState) + String(" -> ") + getStateName(currentState);

  JsonObject wifi = doc.createNestedObject("wifi");
  wifi["connected"] = wifiConnected;
  wifi["mode"] = wifiApMode ? "AP" : "STA";
  wifi["ssid"] = wifiApMode ? WIFI_AP_SSID : WIFI_STA_SSID;
  wifi["ip"] = currentIpString();
}

// ===== AUDIO HELPERS =====
void initAudio() {
  mp3.begin(9600);
  delay(500);
  mp3.sendCommand(CMD_SEL_DEV, 0, 2); // select SD-card
  delay(500);
  mp3.setVol(25);
  audioReady = true;
  Serial.println("🔊 MP3 module ready");
}

void playTrack(uint8_t track, const char *label) {
  if (!audioReady) return;
  mp3.play(track);
  if (label) {
    Serial.printf("🔊 Audio: %s (track %u)\n", label, track);
  } else {
    Serial.printf("🔊 Audio: track %u\n", track);
  }
}

void updateIdleAudio() {
  if (!audioReady) return;
  bool doorClosed = isButtonPressed(limitSwitch);
  bool shouldIdle = (currentState == STATE_READY && doorClosed && !trapDetected);
  if (!shouldIdle) return;
  unsigned long now = millis();
  if (now - lastIdleLoopMs >= IDLE_LOOP_INTERVAL_MS) {
    lastIdleLoopMs = now;
    playTrack(kTrackIdleLoop, "Idle loop");
  }
}

void stopRadarTone() {
  ledcWriteTone(RADAR_BUZZER_CHANNEL, 0);
  radarToneActive = false;
}

void updateRadarAudio() {
  bool doorOpen = !isButtonPressed(limitSwitch);
  if (!doorOpen || trapDetected || currentDistance <= 0 || currentDistance > 150 ||
      currentState >= STATE_TRAP_DETECTED) {
    if (radarToneActive) {
      stopRadarTone();
    }
    return;
  }

  int clampedDistance = constrain(currentDistance, 5, 120);
  int freq = map(clampedDistance, 120, 5, 400, 2200);
  ledcWriteTone(RADAR_BUZZER_CHANNEL, freq);
  radarToneActive = true;
}

// ===== BUTTON STATE MANAGEMENT =====
void updateButtonStates() {
  updateButtonState(powerButton, POWER_BUTTON_PIN);
  updateButtonState(greenSwitch, GREEN_SWITCH_PIN);
  updateButtonState(redSwitch, RED_SWITCH_PIN);
  updateButtonState(orangeSwitch, ORANGE_SWITCH_PIN);
  updateButtonState(leverSwitch, LEVER_SWITCH_PIN);
  updateButtonState(limitSwitch, LIMIT_SWITCH_PIN);
}

void updateButtonState(ButtonState &button, int pin) {
  bool reading = digitalRead(pin);
  unsigned long currentTime = millis();

  if (reading != button.lastState) {
    button.lastChangeTime = currentTime;
  }

  if ((currentTime - button.lastChangeTime) > DEBOUNCE_DELAY_MS) {
    if (reading != button.stableState) {
      button.stableState = reading;
    }
  }

  button.lastState = reading;
}

bool isButtonPressed(const ButtonState &button) {
  return (button.stableState == LOW);
}

bool isButtonJustPressed(ButtonState &button) {
  if (button.stableState == LOW && button.currentState == HIGH) {
    button.currentState = LOW;
    return true;
  } else if (button.stableState == HIGH) {
    button.currentState = HIGH;
  }
  return false;
}

bool isButtonJustReleased(ButtonState &button) {
  if (button.stableState == HIGH && button.currentState == LOW) {
    button.currentState = HIGH;
    return true;
  }
  return false;
}

// ===== SENSOR MANAGEMENT =====
void updateSensors() {
  unsigned long currentTime = millis();

  if (currentTime - lastSensorRead >= SENSOR_READ_INTERVAL_MS) {
    lastSensorRead = currentTime;
    currentDistance = measureDistance();
    trapDetected = (currentDistance > 0 && currentDistance < TRAP_DISTANCE_THRESHOLD_CM);
    updateRadarAudio();
  }
}

int measureDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, ULTRASONIC_TIMEOUT_US);
  if (duration == 0) {
    return -1;
  }

  int distance = duration * 0.034 / 2;
  return distance;
}

// ===== STATE MACHINE =====
void handleStateMachine() {
  SystemState newState = currentState;

  switch (currentState) {
    case STATE_OFF:
      newState = handleStateOff();
      break;
    case STATE_READY:
      newState = handleStateReady();
      break;
    case STATE_DOOR_OPEN:
      newState = handleStateDoorOpen();
      break;
    case STATE_TRAP_DETECTED:
      newState = handleStateTrapDetected();
      break;
    case STATE_DOOR_CLOSED:
      newState = handleStateDoorClosed();
      break;
    case STATE_RED_PRESSED:
      newState = handleStateRedPressed();
      break;
    case STATE_ORANGE_PRESSED:
      newState = handleStateOrangePressed();
      break;
    case STATE_LEVER_DOWN:
      newState = handleStateLeverDown();
      break;
    case STATE_PROCESSING:
      newState = handleStateProcessing();
      break;
    case STATE_ERROR:
      newState = handleStateError();
      break;
  }

  if (newState != currentState) {
    changeState(newState);
  }

  if (isButtonJustPressed(greenSwitch)) {
    playTrack(kTrackGreenButton, "Green button");
  }

  if (isButtonJustPressed(powerButton) && currentState != STATE_OFF) {
    playTrack(kTrackPowerDown, "Power down");
    changeState(STATE_OFF);
  }
}

// ===== STATE HANDLERS =====
SystemState handleStateOff() {
  if (isButtonJustPressed(powerButton)) {
    Serial.println("✅ SYSTEM POWERED ON - Ready Mode");
    playTrack(kTrackStartup, "Power on");
    return STATE_READY;
  }
  return STATE_OFF;
}

SystemState handleStateReady() {
  if (!isButtonPressed(limitSwitch)) {
    Serial.println("🚪 DOOR OPENED - Waiting for Trap Insertion");
    return STATE_DOOR_OPEN;
  }
  return STATE_READY;
}

SystemState handleStateDoorOpen() {
  if (trapDetected) {
    Serial.println("👻 TRAP DETECTED - Close Door to Continue");
    Serial.printf("📏 Distance: %d cm\n", currentDistance);
    return STATE_TRAP_DETECTED;
  }

  if (isButtonPressed(limitSwitch)) {
    Serial.println("⚠️ Door closed without trap - Returning to Ready");
    return STATE_READY;
  }

  return STATE_DOOR_OPEN;
}

SystemState handleStateTrapDetected() {
  if (isButtonPressed(limitSwitch)) {
    Serial.println("🚪 DOOR CLOSED WITH TRAP - Press Red Button to Proceed");
    return STATE_DOOR_CLOSED;
  }

  if (!trapDetected) {
    Serial.println("❌ Trap removed - Waiting for trap insertion");
    return STATE_DOOR_OPEN;
  }

  return STATE_TRAP_DETECTED;
}

SystemState handleStateDoorClosed() {
  if (isButtonJustPressed(redSwitch)) {
    Serial.println("🔴 RED BUTTON PRESSED - Press Orange Button");
    return STATE_RED_PRESSED;
  }

  if (!isButtonPressed(limitSwitch)) {
    Serial.println("🚪 Door reopened - Returning to door open state");
    return STATE_DOOR_OPEN;
  }

  return STATE_DOOR_CLOSED;
}

SystemState handleStateRedPressed() {
  if (isButtonJustPressed(orangeSwitch)) {
    Serial.println("🟠 ORANGE BUTTON PRESSED - Throw Lever Down");
    return STATE_ORANGE_PRESSED;
  }
  return STATE_RED_PRESSED;
}

SystemState handleStateOrangePressed() {
  if (isButtonJustPressed(leverSwitch)) {
    Serial.println("🔄 LEVER THROWN DOWN - Activating Smoke Effect!");
    smokeStartTime = millis();
    return STATE_LEVER_DOWN;
  }
  return STATE_ORANGE_PRESSED;
}

SystemState handleStateLeverDown() {
  unsigned long currentTime = millis();

  if (currentTime - smokeStartTime >= SMOKE_DURATION_MS) {
    Serial.println("💨 Smoke effect complete - Throw Lever Up to Reset");
    return STATE_PROCESSING;
  }

  return STATE_LEVER_DOWN;
}

SystemState handleStateProcessing() {
  if (isButtonJustReleased(leverSwitch)) {
    Serial.println("🔼 LEVER THROWN UP - CONTAINMENT COMPLETE!");
    Serial.println("🔄 System Ready for Next Operation");
    playTrack(kTrackTrapClean, "Trap clean");
    return STATE_READY;
  }
  return STATE_PROCESSING;
}

SystemState handleStateError() {
  if (isButtonJustPressed(powerButton)) {
    Serial.println("🔧 Error cleared - Returning to Ready");
    return STATE_READY;
  }
  return STATE_ERROR;
}

// ===== STATE MANAGEMENT =====
void changeState(SystemState newState) {
  previousState = currentState;
  currentState = newState;

  Serial.printf("🔄 State: %s → %s\n",
                getStateName(previousState).c_str(),
                getStateName(newState).c_str());

  onStateEnter(newState);
}

void onStateEnter(SystemState state) {
  switch (state) {
    case STATE_OFF:
      resetSystem();
      break;
    case STATE_READY:
      break;
    case STATE_DOOR_OPEN:
      playTrack(kTrackDoorOpen, "Door open");
      break;
    case STATE_TRAP_DETECTED:
      playTrack(kTrackTrapInsert, "Trap detected");
      break;
    case STATE_DOOR_CLOSED:
      playTrack(kTrackDoorClose, "Door closed");
      break;
    case STATE_RED_PRESSED:
      playTrack(kTrackButtonRed, "Red button");
      break;
    case STATE_ORANGE_PRESSED:
      playTrack(kTrackButtonOrange, "Orange button");
      break;
    case STATE_LEVER_DOWN:
      smokeStartTime = millis();
      playTrack(kTrackEPA, "Lever down EPA");
      break;
    case STATE_PROCESSING:
      break;
    case STATE_ERROR:
      Serial.println("❌ ERROR STATE ENTERED");
      break;
  }
}

// ===== OUTPUT MANAGEMENT =====
void updateOutputs() {
  updateLEDs();
  updateRelays();
}

void updateLEDs() {
  bool greenLED = false;
  bool redLED = false;

  switch (currentState) {
    case STATE_OFF:
      break;
    case STATE_READY:
      greenLED = true;
      break;
    case STATE_DOOR_OPEN:
      greenLED = (millis() % 1000) < 500;
      break;
    case STATE_TRAP_DETECTED:
    case STATE_DOOR_CLOSED:
      redLED = true;
      break;
    case STATE_RED_PRESSED:
    case STATE_ORANGE_PRESSED:
      redLED = (millis() % 200) < 100;
      break;
    case STATE_LEVER_DOWN:
      greenLED = true;
      redLED = true;
      break;
    case STATE_PROCESSING:
      greenLED = true;
      break;
    case STATE_ERROR:
      greenLED = redLED = (millis() % 100) < 50;
      break;
  }

  digitalWrite(GREEN_LED_PIN, greenLED);
  digitalWrite(RED_LED_PIN, redLED);
}

void updateRelays() {
  bool greenSwitchOn = (currentState == STATE_READY || currentState == STATE_PROCESSING);
  digitalWrite(RELAY_GREEN_PIN, greenSwitchOn ? RELAY_ON : RELAY_OFF);

  bool redSwitchOn = (currentState == STATE_RED_PRESSED || currentState == STATE_ORANGE_PRESSED);
  digitalWrite(RELAY_RED_PIN, redSwitchOn ? RELAY_ON : RELAY_OFF);

  bool orangeSwitchOn = (currentState == STATE_ORANGE_PRESSED);
  digitalWrite(RELAY_ORANGE_PIN, orangeSwitchOn ? RELAY_ON : RELAY_OFF);

  bool smokeOn = (currentState == STATE_LEVER_DOWN);
  digitalWrite(RELAY_SMOKE_PIN, smokeOn ? RELAY_ON : RELAY_OFF);

  bool lockOn = (currentState >= STATE_DOOR_CLOSED && currentState <= STATE_PROCESSING);
  digitalWrite(RELAY_LOCK_PIN, lockOn ? RELAY_ON : RELAY_OFF);
}

// ===== UTILITY FUNCTIONS =====
String getStateName(SystemState state) {
  switch (state) {
    case STATE_OFF: return "OFF";
    case STATE_READY: return "READY";
    case STATE_DOOR_OPEN: return "DOOR_OPEN";
    case STATE_TRAP_DETECTED: return "TRAP_DETECTED";
    case STATE_DOOR_CLOSED: return "DOOR_CLOSED";
    case STATE_RED_PRESSED: return "RED_PRESSED";
    case STATE_ORANGE_PRESSED: return "ORANGE_PRESSED";
    case STATE_LEVER_DOWN: return "LEVER_DOWN";
    case STATE_PROCESSING: return "PROCESSING";
    case STATE_ERROR: return "ERROR";
    default: return "UNKNOWN";
  }
}

void resetSystem() {
  digitalWrite(GREEN_LED_PIN, LOW);
  digitalWrite(RED_LED_PIN, LOW);
  digitalWrite(RELAY_GREEN_PIN, RELAY_OFF);
  digitalWrite(RELAY_RED_PIN, RELAY_OFF);
  digitalWrite(RELAY_ORANGE_PIN, RELAY_OFF);
  digitalWrite(RELAY_SMOKE_PIN, RELAY_OFF);
  digitalWrite(RELAY_LOCK_PIN, RELAY_OFF);

  trapDetected = false;
  currentDistance = -1;
  stopRadarTone();
  lastIdleLoopMs = millis();

  Serial.println("🔄 System Reset Complete");
}

