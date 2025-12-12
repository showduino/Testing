// Showduino CYD UI (LVGL) – Brain REST client
//
// Based on the Showduino UI controller skeleton:
//   https://github.com/showduino/show-finally-fin/blob/main/Showduino3/UI_Controller/Showduino_UI_Controller/Showduino_UI_Controller.ino
//
// This sketch is UI-only:
// - LVGL + touch + display
// - Wi-Fi client to the Brain (ESP32-S3)
// - Calls Brain REST API: /api/status, /api/props, /api/shows, /api/play, /api/stop
//
// NOTE: This is a starter skeleton. You may need to adjust pinout for your exact CYD board.

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>

#include <Arduino_GFX_Library.h>
#include <lvgl.h>

// ---------------------- USER CONFIG ----------------------
static const char* BRAIN_HOST = "http://192.168.4.1"; // Brain AP default gateway; change if needed
static const char* WIFI_SSID  = "ShowduinoBrain";
static const char* WIFI_PASS  = "showduino";

// Polling intervals
static const uint32_t POLL_STATUS_MS = 500;
static const uint32_t POLL_PROPS_MS  = 800;
static const uint32_t POLL_SHOWS_MS  = 2000;

// ---------------------- DISPLAY (PLACEHOLDER) ----------------------
// You MUST set up Arduino_GFX for your CYD board.
// Many CYD boards are ILI9488 / ST7796 over SPI with XPT2046 touch.
// This file leaves display driver as TODO, because CYD variants differ.

static Arduino_GFX* gfx = nullptr;

// ---------------------- LVGL ----------------------
static lv_disp_draw_buf_t draw_buf;
static lv_color_t* buf1 = nullptr;
static lv_color_t* buf2 = nullptr;

static lv_obj_t* lblStatus = nullptr;
static lv_obj_t* lblProps  = nullptr;
static lv_obj_t* lblShows  = nullptr;
static lv_obj_t* taShow    = nullptr;
static lv_obj_t* txtShowName = nullptr;

static String httpGetText(const String& url) {
  HTTPClient http;
  http.setTimeout(800);
  if (!http.begin(url)) return "";
  int code = http.GET();
  String body = (code > 0) ? http.getString() : "";
  http.end();
  return body;
}

static bool httpPostText(const String& url, const String& body, const char* contentType = "application/json") {
  HTTPClient http;
  http.setTimeout(1200);
  if (!http.begin(url)) return false;
  http.addHeader("Content-Type", contentType);
  int code = http.POST((uint8_t*)body.c_str(), body.length());
  http.end();
  return code > 0 && code < 400;
}

static bool httpPostEmpty(const String& url) {
  HTTPClient http;
  http.setTimeout(1200);
  if (!http.begin(url)) return false;
  int code = http.POST("");
  http.end();
  return code > 0 && code < 400;
}

static void uiSetLabel(lv_obj_t* lbl, const String& s) {
  if (!lbl) return;
  lv_label_set_text(lbl, s.c_str());
}

static void onPlay(lv_event_t* e) {
  (void)e;
  const char* n = lv_textarea_get_text(txtShowName);
  String url = String(BRAIN_HOST) + "/api/play?name=" + String(n ? n : "");
  httpPostEmpty(url);
}

static void onStop(lv_event_t* e) {
  (void)e;
  String url = String(BRAIN_HOST) + "/api/stop";
  httpPostEmpty(url);
}

static void buildUI() {
  lv_obj_t* scr = lv_scr_act();

  lv_obj_t* top = lv_obj_create(scr);
  lv_obj_set_size(top, lv_pct(100), 60);
  lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 0);

  txtShowName = lv_textarea_create(top);
  lv_obj_set_size(txtShowName, 160, 40);
  lv_obj_align(txtShowName, LV_ALIGN_LEFT_MID, 8, 0);
  lv_textarea_set_one_line(txtShowName, true);
  lv_textarea_set_text(txtShowName, "Haunt1");

  lv_obj_t* btnPlay = lv_btn_create(top);
  lv_obj_set_size(btnPlay, 90, 40);
  lv_obj_align(btnPlay, LV_ALIGN_LEFT_MID, 180, 0);
  lv_obj_add_event_cb(btnPlay, onPlay, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lblP = lv_label_create(btnPlay);
  lv_label_set_text(lblP, "PLAY");
  lv_obj_center(lblP);

  lv_obj_t* btnStop = lv_btn_create(top);
  lv_obj_set_size(btnStop, 90, 40);
  lv_obj_align(btnStop, LV_ALIGN_LEFT_MID, 280, 0);
  lv_obj_add_event_cb(btnStop, onStop, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lblS = lv_label_create(btnStop);
  lv_label_set_text(lblS, "STOP");
  lv_obj_center(lblS);

  lv_obj_t* body = lv_obj_create(scr);
  lv_obj_set_size(body, lv_pct(100), lv_pct(100));
  lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 70);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  auto makePanel = [&](const char* title) -> lv_obj_t* {
    lv_obj_t* p = lv_obj_create(body);
    lv_obj_set_size(p, lv_pct(33), lv_pct(100));
    lv_obj_t* t = lv_label_create(p);
    lv_label_set_text(t, title);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 6, 4);
    return p;
  };

  lv_obj_t* p1 = makePanel("STATUS");
  lblStatus = lv_label_create(p1);
  lv_obj_align(lblStatus, LV_ALIGN_TOP_LEFT, 6, 28);
  lv_label_set_long_mode(lblStatus, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lblStatus, lv_pct(100));

  lv_obj_t* p2 = makePanel("PROPS");
  lblProps = lv_label_create(p2);
  lv_obj_align(lblProps, LV_ALIGN_TOP_LEFT, 6, 28);
  lv_label_set_long_mode(lblProps, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lblProps, lv_pct(100));

  lv_obj_t* p3 = makePanel("SHOWS");
  lblShows = lv_label_create(p3);
  lv_obj_align(lblShows, LV_ALIGN_TOP_LEFT, 6, 28);
  lv_label_set_long_mode(lblShows, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lblShows, lv_pct(100));
}

// LVGL flush callback
static void my_flush_cb(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* color_p) {
  if (!gfx) {
    lv_disp_flush_ready(disp);
    return;
  }
  const int32_t w = area->x2 - area->x1 + 1;
  const int32_t h = area->y2 - area->y1 + 1;
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t*)color_p, w, h);
  lv_disp_flush_ready(disp);
}

static void initLVGL() {
  lv_init();

  // Allocate double buffer in PSRAM if available
  const uint32_t bufPixels = 320 * 20; // small default; tune for your display
  buf1 = (lv_color_t*)heap_caps_malloc(bufPixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  buf2 = (lv_color_t*)heap_caps_malloc(bufPixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf1 || !buf2) {
    buf1 = (lv_color_t*)malloc(bufPixels * sizeof(lv_color_t));
    buf2 = (lv_color_t*)malloc(bufPixels * sizeof(lv_color_t));
  }
  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, bufPixels);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.flush_cb = my_flush_cb;
  disp_drv.draw_buf = &draw_buf;

  // TODO: set these to your CYD resolution
  disp_drv.hor_res = 480;
  disp_drv.ver_res = 320;

  lv_disp_drv_register(&disp_drv);
}

static void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

static uint32_t lastStatus = 0;
static uint32_t lastProps = 0;
static uint32_t lastShows = 0;

static void poll() {
  const uint32_t now = millis();
  if (WiFi.status() != WL_CONNECTED) return;

  if (now - lastStatus >= POLL_STATUS_MS) {
    lastStatus = now;
    String s = httpGetText(String(BRAIN_HOST) + "/api/status");
    if (s.length()) uiSetLabel(lblStatus, s);
  }

  if (now - lastProps >= POLL_PROPS_MS) {
    lastProps = now;
    String s = httpGetText(String(BRAIN_HOST) + "/api/props");
    if (s.length()) uiSetLabel(lblProps, s);
  }

  if (now - lastShows >= POLL_SHOWS_MS) {
    lastShows = now;
    String s = httpGetText(String(BRAIN_HOST) + "/api/shows");
    if (s.length()) uiSetLabel(lblShows, s);
  }
}

void setup() {
  Serial.begin(115200);

  // TODO: Initialize your CYD display into `gfx`.
  // Once you have gfx initialized, call gfx->begin() and gfx->fillScreen(0x0000).

  initLVGL();
  buildUI();
  initWiFi();
}

void loop() {
  lv_timer_handler();
  poll();
  delay(5);
}
