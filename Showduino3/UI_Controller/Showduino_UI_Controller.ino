#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include <TAMC_GT911.h>
#include <Wire.h>

#include "../common/DuoFrame.h"

using namespace showduino;

// ────────────────────────────────────────────────────────────────
//  Display + Touch Pinout (JC8048W550C)
// ────────────────────────────────────────────────────────────────
#define TFT_BL 2
#define TFT_DE 40
#define TFT_VSYNC 41
#define TFT_HSYNC 39
#define TFT_PCLK 42
#define TFT_R1 45
#define TFT_R2 48
#define TFT_R3 47
#define TFT_R4 21
#define TFT_R5 14
#define TFT_G0 5
#define TFT_G1 6
#define TFT_G2 7
#define TFT_G3 15
#define TFT_G4 16
#define TFT_G5 4
#define TFT_B1 8
#define TFT_B2 3
#define TFT_B3 46
#define TFT_B4 9
#define TFT_B5 1

#define TOUCH_SDA 19
#define TOUCH_SCL 20
#define TOUCH_INT 38
#define TOUCH_RST 10

#define SCREEN_W 800
#define SCREEN_H 480
#define DRAW_BUF_LINES 40
#define DRAW_BUF_SIZE (SCREEN_W * DRAW_BUF_LINES)

#define RAW_X_MIN 30
#define RAW_X_MAX 271
#define RAW_Y_MIN 18
#define RAW_Y_MAX 450

// Theme colors
#define CLR_GORE_BLACK 0x111111
#define CLR_GORE_RED 0xA20000
#define CLR_GORE_ACCENT 0x550000
#define CLR_GORE_PANEL 0x1A1A1A
#define CLR_GORE_TEXT 0xE0E0E0

// Update this with the Brain S3 MAC address
uint8_t BRAIN_PEER_MAC[6] = {0x24, 0x6F, 0x28, 0xAA, 0xBB, 0xCC};

Arduino_ESP32RGBPanel *rgbPanel = nullptr;
Arduino_RGB_Display *gfx = nullptr;
TAMC_GT911 *touch = nullptr;

static lv_color_t *buf1 = nullptr;
static lv_color_t *buf2 = nullptr;
static lv_disp_draw_buf_t drawBuf;

// UI objects
static lv_obj_t *screen_live = nullptr;
static lv_obj_t *modeSwitch = nullptr;
static lv_obj_t *statusLabel = nullptr;
static lv_obj_t *timelineSlider = nullptr;
static lv_obj_t *relayButtons[8]{};
static lv_obj_t *heartbeatLed = nullptr;
static lv_obj_t *linkBadge = nullptr;

struct BrainStatus {
  bool online = false;
  bool manualMode = true;
  uint32_t lastHeartbeat = 0;
  uint32_t timelinePos = 0;
  uint8_t relays[8]{};
};

BrainStatus brainStatus;

// ────────────────────────────────────────────────────────────────
//  LVGL Hooks
// ────────────────────────────────────────────────────────────────
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  if (!gfx) {
    lv_disp_flush_ready(disp);
    return;
  }

  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
  lv_disp_flush_ready(disp);
}

void my_touchpad_read(lv_indev_drv_t *driver, lv_indev_data_t *data) {
  if (!touch) {
    data->state = LV_INDEV_STATE_REL;
    return;
  }

  touch->read();
  if (touch->isTouched) {
    int16_t rawX = touch->points[0].x;
    int16_t rawY = touch->points[0].y;

    int16_t x = map(rawY, RAW_Y_MAX, RAW_Y_MIN, 0, SCREEN_W);
    int16_t y = map(rawX, RAW_X_MIN, RAW_X_MAX, 0, SCREEN_H);
    data->point.x = constrain(x, 0, SCREEN_W - 1);
    data->point.y = constrain(y, 0, SCREEN_H - 1);
    data->state = LV_INDEV_STATE_PR;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

bool init_lvgl() {
  lv_init();

  buf1 = (lv_color_t *)ps_malloc(sizeof(lv_color_t) * DRAW_BUF_SIZE);
  buf2 = (lv_color_t *)ps_malloc(sizeof(lv_color_t) * DRAW_BUF_SIZE);
  if (!buf1 || !buf2) {
    Serial.println("[LVGL] Buffer allocation failed");
    return false;
  }

  lv_disp_draw_buf_init(&drawBuf, buf1, buf2, DRAW_BUF_SIZE);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = SCREEN_W;
  disp_drv.ver_res = SCREEN_H;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &drawBuf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);

  return true;
}

// ────────────────────────────────────────────────────────────────
//  ESP-NOW Helpers
// ────────────────────────────────────────────────────────────────
bool sendDuoFrame(DuoFrameCommand cmd, const uint8_t *payload, uint8_t len) {
  DuoFrame frame;
  frame.command = cmd;
  frame.length = len;
  if (payload && len > 0) {
    memcpy(frame.payload, payload, len);
  }

  uint8_t buffer[DUOFRAME_MAX_PAYLOAD + 4];
  size_t packetLen = duoFrameSerialize(frame, buffer, sizeof(buffer));
  if (packetLen == 0) {
    return false;
  }
  esp_err_t err = esp_now_send(BRAIN_PEER_MAC, buffer, packetLen);
  return err == ESP_OK;
}

void onEspNowSent(const uint8_t *mac, esp_now_send_status_t status) {
  if (status != ESP_NOW_SEND_SUCCESS) {
    Serial.println("[ESP-NOW] Send failed");
  }
}

void refreshRelayButtons() {
  for (uint8_t i = 0; i < 8; ++i) {
    if (!relayButtons[i]) continue;
    bool active = brainStatus.relays[i];
    lv_obj_set_style_bg_color(relayButtons[i],
                              lv_color_hex(active ? CLR_GORE_RED : CLR_GORE_PANEL), 0);
  }
}

void updateStatusLabel() {
  if (!statusLabel) return;
  char line[128];
  snprintf(line, sizeof(line),
           "Link: %s | Mode: %s | Timeline: %lums",
           brainStatus.online ? "ONLINE" : "OFFLINE",
           brainStatus.manualMode ? "MANUAL" : "AUTO",
           brainStatus.timelinePos);
  lv_label_set_text(statusLabel, line);

  if (linkBadge) {
    lv_obj_set_style_bg_color(linkBadge,
                              lv_color_hex(brainStatus.online ? 0x006400 : 0x4A0000), 0);
    lv_label_set_text(linkBadge, brainStatus.online ? "LINKED" : "SEARCHING");
  }
}

void onEspNowRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  DuoFrame frame;
  if (!duoFrameParse(incomingData, len, frame)) {
    Serial.println("[ESP-NOW] Invalid DuoFrame");
    return;
  }

  switch (frame.command) {
    case DF_CMD_HEARTBEAT:
      brainStatus.online = true;
      brainStatus.lastHeartbeat = millis();
      if (heartbeatLed) {
        lv_obj_set_style_bg_color(heartbeatLed, lv_color_hex(0x00FF00), 0);
        lv_timer_t *timer = lv_timer_create(
            [](lv_timer_t *t) {
              lv_obj_t *led = (lv_obj_t *)t->user_data;
              lv_obj_set_style_bg_color(led, lv_color_hex(CLR_GORE_ACCENT), 0);
              lv_timer_del(t);
            },
            120, heartbeatLed);
        (void)timer;
      }
      break;

    case DF_CMD_STATUS:
      if (frame.length >= 13) {
        brainStatus.manualMode = frame.payload[0];
        memcpy(brainStatus.relays, &frame.payload[1], 8);
        brainStatus.timelinePos = ((uint32_t)frame.payload[9] << 24) |
                                  ((uint32_t)frame.payload[10] << 16) |
                                  ((uint32_t)frame.payload[11] << 8) |
                                  ((uint32_t)frame.payload[12]);
        refreshRelayButtons();
        if (timelineSlider) {
          uint32_t sliderMax = lv_slider_get_max_value(timelineSlider);
          uint32_t clamped = min(brainStatus.timelinePos, sliderMax);
          lv_slider_set_value(timelineSlider, clamped, LV_ANIM_OFF);
        }
        if (modeSwitch) {
          if (brainStatus.manualMode) {
            lv_obj_add_state(modeSwitch, LV_STATE_CHECKED);
          } else {
            lv_obj_clear_state(modeSwitch, LV_STATE_CHECKED);
          }
        }
      }
      break;

    default:
      break;
  }

  updateStatusLabel();
}

bool initEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, true);
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] Init failed");
    return false;
  }

  esp_now_register_send_cb(onEspNowSent);
  esp_now_register_recv_cb(onEspNowRecv);

  esp_now_peer_info_t peerInfo{};
  memcpy(peerInfo.peer_addr, BRAIN_PEER_MAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("[ESP-NOW] Failed to add Brain peer");
    return false;
  }

  return true;
}

// ────────────────────────────────────────────────────────────────
//  UI Events
// ────────────────────────────────────────────────────────────────
void sendRelayToggle(uint8_t relayIdx) {
  uint8_t payload[2] = {relayIdx, (uint8_t)!brainStatus.relays[relayIdx]};
  if (sendDuoFrame(DF_CMD_RELAY_SET, payload, sizeof(payload))) {
    Serial.printf("[UI] Relay %u -> %u\n", relayIdx, payload[1]);
  }
}

void sendModeChange(bool manual) {
  uint8_t payload[1] = {static_cast<uint8_t>(manual)};
  sendDuoFrame(DF_CMD_CONTROL_MODE, payload, sizeof(payload));
}

void sendTimelineSeek(uint32_t positionMs, bool livePlay) {
  uint8_t payload[5];
  payload[0] = livePlay ? 1 : 0;
  payload[1] = (positionMs >> 24) & 0xFF;
  payload[2] = (positionMs >> 16) & 0xFF;
  payload[3] = (positionMs >> 8) & 0xFF;
  payload[4] = positionMs & 0xFF;
  sendDuoFrame(DF_CMD_TIMELINE_SEEK, payload, sizeof(payload));
}

void createRelayGrid(lv_obj_t *parent) {
  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(parent, 16, 0);
  lv_obj_set_style_pad_column(parent, 16, 0);

  for (uint8_t i = 0; i < 8; ++i) {
    relayButtons[i] = lv_btn_create(parent);
    lv_obj_set_size(relayButtons[i], 150, 70);
    lv_obj_set_style_bg_color(relayButtons[i], lv_color_hex(CLR_GORE_PANEL), 0);
    lv_obj_set_style_shadow_width(relayButtons[i], 10, 0);
    lv_obj_set_style_shadow_color(relayButtons[i], lv_color_hex(CLR_GORE_ACCENT), 0);

    lv_obj_t *label = lv_label_create(relayButtons[i]);
    char txt[32];
    snprintf(txt, sizeof(txt), "RELAY %u", i + 1);
    lv_label_set_text(label, txt);
    lv_obj_center(label);

    lv_obj_add_event_cb(
        relayButtons[i],
        [](lv_event_t *e) {
          uint8_t index = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
          if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            sendRelayToggle(index);
          }
        },
        LV_EVENT_ALL, (void *)(uintptr_t)i);
  }
}

void create_live_screen() {
  screen_live = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen_live, lv_color_hex(CLR_GORE_BLACK), 0);
  lv_obj_set_style_bg_grad_color(screen_live, lv_color_hex(CLR_GORE_ACCENT), 0);
  lv_obj_set_style_bg_grad_dir(screen_live, LV_GRAD_DIR_VER, 0);

  lv_obj_t *topBar = lv_obj_create(screen_live);
  lv_obj_set_size(topBar, SCREEN_W - 40, 80);
  lv_obj_align(topBar, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_set_style_bg_color(topBar, lv_color_hex(CLR_GORE_PANEL), 0);
  lv_obj_set_style_border_width(topBar, 0, 0);

  lv_obj_t *title = lv_label_create(topBar);
  lv_label_set_text(title, "SHOWDUINO LIVE DESK");
  lv_obj_set_style_text_color(title, lv_color_hex(CLR_GORE_TEXT), 0);
  lv_obj_align(title, LV_ALIGN_LEFT_MID, 20, 0);

  linkBadge = lv_label_create(topBar);
  lv_label_set_text(linkBadge, "SEARCHING");
  lv_obj_set_style_text_color(linkBadge, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(linkBadge, LV_ALIGN_RIGHT_MID, -20, 0);

  heartbeatLed = lv_obj_create(topBar);
  lv_obj_set_size(heartbeatLed, 16, 16);
  lv_obj_set_style_bg_color(heartbeatLed, lv_color_hex(CLR_GORE_ACCENT), 0);
  lv_obj_set_style_radius(heartbeatLed, LV_RADIUS_CIRCLE, 0);
  lv_obj_align(heartbeatLed, LV_ALIGN_RIGHT_MID, -150, 0);

  // Mode switch panel
  lv_obj_t *modePanel = lv_obj_create(screen_live);
  lv_obj_set_size(modePanel, SCREEN_W - 40, 70);
  lv_obj_align(modePanel, LV_ALIGN_TOP_MID, 0, 110);
  lv_obj_set_style_bg_color(modePanel, lv_color_hex(CLR_GORE_PANEL), 0);
  lv_obj_set_style_border_color(modePanel, lv_color_hex(CLR_GORE_RED), 0);
  lv_obj_set_style_border_width(modePanel, 2, 0);
  lv_obj_set_layout(modePanel, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(modePanel, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_all(modePanel, 12, 0);
  lv_obj_set_style_pad_column(modePanel, 20, 0);

  lv_obj_t *modeLabel = lv_label_create(modePanel);
  lv_label_set_text(modeLabel, "Manual Override");
  lv_obj_set_style_text_color(modeLabel, lv_color_hex(CLR_GORE_TEXT), 0);

  modeSwitch = lv_switch_create(modePanel);
  lv_obj_add_state(modeSwitch, LV_STATE_CHECKED);
  lv_obj_add_event_cb(
      modeSwitch,
      [](lv_event_t *e) {
        if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
          bool manual = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
          sendModeChange(manual);
        }
      },
      LV_EVENT_VALUE_CHANGED, NULL);

  // Timeline slider
  lv_obj_t *timelinePanel = lv_obj_create(screen_live);
  lv_obj_set_size(timelinePanel, SCREEN_W - 40, 120);
  lv_obj_align(timelinePanel, LV_ALIGN_TOP_MID, 0, 200);
  lv_obj_set_style_bg_color(timelinePanel, lv_color_hex(CLR_GORE_PANEL), 0);
  lv_obj_set_style_border_width(timelinePanel, 0, 0);

  lv_obj_t *timelineLabel = lv_label_create(timelinePanel);
  lv_label_set_text(timelineLabel, LV_SYMBOL_PLAY " TIMELINE");
  lv_obj_set_style_text_color(timelineLabel, lv_color_hex(CLR_GORE_TEXT), 0);
  lv_obj_align(timelineLabel, LV_ALIGN_TOP_LEFT, 20, 10);

  timelineSlider = lv_slider_create(timelinePanel);
  lv_obj_set_size(timelineSlider, SCREEN_W - 120, 40);
  lv_obj_align(timelineSlider, LV_ALIGN_BOTTOM_MID, 0, -20);
  lv_slider_set_range(timelineSlider, 0, 180000);
  lv_obj_add_event_cb(
      timelineSlider,
      [](lv_event_t *e) {
        if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
          uint32_t pos = lv_slider_get_value(lv_event_get_target(e));
          sendTimelineSeek(pos, lv_obj_has_state(modeSwitch, LV_STATE_CHECKED));
        }
      },
      LV_EVENT_RELEASED, NULL);

  // Relay grid
  lv_obj_t *relayPanel = lv_obj_create(screen_live);
  lv_obj_set_size(relayPanel, SCREEN_W - 40, 210);
  lv_obj_align(relayPanel, LV_ALIGN_BOTTOM_MID, 0, -90);
  lv_obj_set_style_bg_color(relayPanel, lv_color_hex(CLR_GORE_PANEL), 0);
  lv_obj_set_style_border_width(relayPanel, 0, 0);
  lv_obj_set_style_pad_all(relayPanel, 20, 0);

  createRelayGrid(relayPanel);

  // Status footer
  statusLabel = lv_label_create(screen_live);
  lv_obj_align(statusLabel, LV_ALIGN_BOTTOM_LEFT, 20, -20);
  lv_obj_set_style_text_color(statusLabel, lv_color_hex(CLR_GORE_TEXT), 0);
  lv_label_set_text(statusLabel, "Link: OFFLINE | Mode: MANUAL | Timeline: 0ms");

  lv_scr_load(screen_live);
}

// ────────────────────────────────────────────────────────────────
//  Boot Helpers
// ────────────────────────────────────────────────────────────────
bool initDisplay() {
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW);

  rgbPanel = new Arduino_ESP32RGBPanel(
      TFT_DE, TFT_VSYNC, TFT_HSYNC, TFT_PCLK, TFT_R1, TFT_R2, TFT_R3, TFT_R4, TFT_R5,
      TFT_G0, TFT_G1, TFT_G2, TFT_G3, TFT_G4, TFT_G5, TFT_B1, TFT_B2, TFT_B3, TFT_B4,
      TFT_B5, 0, 40, 48, 40, 0, 13, 3, 29, 1, 16000000);

  if (!rgbPanel) {
    Serial.println("[DISPLAY] RGB panel allocation failed");
    return false;
  }

  gfx = new Arduino_RGB_Display(SCREEN_W, SCREEN_H, rgbPanel);
  if (!gfx || !gfx->begin()) {
    Serial.println("[DISPLAY] Failed to start panel");
    return false;
  }

  gfx->fillScreen(BLACK);
  gfx->setTextColor(0xF800);
  gfx->setTextSize(3);
  gfx->setCursor(140, 200);
  gfx->println("SHOWDUINO STUDIO");
  gfx->setTextSize(2);
  gfx->setCursor(180, 260);
  gfx->println("GORE FX – Live Desk");

  digitalWrite(TFT_BL, HIGH);
  return true;
}

bool initTouch() {
  Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);
  touch = new TAMC_GT911(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, SCREEN_W, SCREEN_H);
  if (!touch) {
    Serial.println("[TOUCH] Allocation failed");
    return false;
  }
  touch->begin();
  touch->setRotation(2);
  return true;
}

// ────────────────────────────────────────────────────────────────
//  Arduino Hooks
// ────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n=== Showduino UI Controller Boot ===");

  if (!initDisplay()) {
    Serial.println("[BOOT] Display failed");
    while (true) delay(1000);
  }

  if (!init_lvgl()) {
    Serial.println("[BOOT] LVGL failed");
    while (true) delay(1000);
  }

  if (!initTouch()) {
    Serial.println("[BOOT] Touch failed");
    while (true) delay(1000);
  }

  create_live_screen();

  if (!initEspNow()) {
    Serial.println("[BOOT] ESP-NOW failed");
    while (true) delay(1000);
  }

  Serial.println("[BOOT] Ready");
}

void loop() {
  lv_tick_inc(5);
  lv_timer_handler();
  delay(5);

  static uint32_t lastPing = 0;
  if (millis() - lastPing > 1000) {
    sendDuoFrame(DF_CMD_HEARTBEAT, nullptr, 0);
    lastPing = millis();
  }

  if (brainStatus.online && millis() - brainStatus.lastHeartbeat > 2500) {
    brainStatus.online = false;
    updateStatusLabel();
  }
}
