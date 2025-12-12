// Showduino UI Addon (LVGL) – JC8048W550C + GT911 (800x480) + SPI microSD
//
// This is UI-only (addon board):
// - Runs LVGL/touch/display smoothly (frequent yields, short network timeouts)
// - Connects to Brain over Wi-Fi and calls Brain REST API:
//   /api/status, /api/props, /api/shows, /api/play, /api/stop, /api/show
// - Uses onboard SPI SD card for caching and optional local storage
//
// Based on skeleton reference:
//   https://github.com/showduino/show-finally-fin/blob/main/Showduino3/UI_Controller/Showduino_UI_Controller/Showduino_UI_Controller.ino

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include <TAMC_GT911.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include <Preferences.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>

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
#define TOUCH_RST 0

// SD Card (SPI interface) - built into display board
// Pin assignments per schematic: JO10=CS, JO11=MOSI, JO12=SCK, JO13=MISO
#define SD_CS_PIN 10
#define SD_MOSI_PIN 11
#define SD_MISO_PIN 13
#define SD_SCK_PIN 12

#define SCREEN_W 800
#define SCREEN_H 480

// LVGL draw buffer tuning (lines of 800px). Increase if you have PSRAM.
#define DRAW_BUF_LINES 60
#define DRAW_BUF_SIZE (SCREEN_W * DRAW_BUF_LINES)

// ---------------------- USER CONFIG ----------------------
static const char* WIFI_SSID = "ShowduinoBrain";
static const char* WIFI_PASS = "showduino";
static const char* BRAIN_HOST = "http://192.168.4.1";

static const uint32_t POLL_STATUS_MS = 500;
static const uint32_t POLL_PROPS_MS  = 800;
static const uint32_t POLL_SHOWS_MS  = 2000;

static const char* CACHE_DIR = "/cache";
static const char* OUTBOX_DIR = "/outbox";

// ---------------------- Theme ----------------------
static const uint32_t CLR_GORE_BLACK = 0x111111;

// ---------------------- Globals ----------------------
Preferences preferences;

Arduino_ESP32RGBPanel* rgbPanel = nullptr;
Arduino_RGB_Display* gfx = nullptr;
TAMC_GT911* touch = nullptr;

static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;
static lv_indev_t* indev = nullptr;

static lv_obj_t* lblStatus = nullptr;
static lv_obj_t* lblProps  = nullptr;
static lv_obj_t* lblShows  = nullptr;
static lv_obj_t* txtShowName = nullptr;
static lv_obj_t* taShowJson = nullptr;
static lv_obj_t* lblEditorMsg = nullptr;

static bool sdOk = false;

// ---------------------- Utilities ----------------------
static String httpGetText(const String& url, uint16_t timeoutMs = 350) {
  HTTPClient http;
  http.setTimeout(timeoutMs);
  if (!http.begin(url)) return "";
  int code = http.GET();
  String body = (code > 0) ? http.getString() : "";
  http.end();
  return body;
}

static bool httpPostEmpty(const String& url, uint16_t timeoutMs = 600) {
  HTTPClient http;
  http.setTimeout(timeoutMs);
  if (!http.begin(url)) return false;
  int code = http.POST("");
  http.end();
  return code > 0 && code < 400;
}

static bool httpPostText(const String& url, const String& body, const char* contentType = "application/json", uint16_t timeoutMs = 1200) {
  HTTPClient http;
  http.setTimeout(timeoutMs);
  if (!http.begin(url)) return false;
  http.addHeader("Content-Type", contentType);
  int code = http.POST((uint8_t*)body.c_str(), body.length());
  http.end();
  return code > 0 && code < 400;
}

static void uiSetLabel(lv_obj_t* lbl, const String& s) {
  if (!lbl) return;
  lv_label_set_text(lbl, s.c_str());
}

static void sdEnsureDir(const char* path) {
  if (!sdOk) return;
  if (!SD.exists(path)) SD.mkdir(path);
}

static void sdWriteText(const char* path, const String& s) {
  if (!sdOk) return;
  File f = SD.open(path, FILE_WRITE);
  if (!f) return;
  f.print(s);
  f.close();
}

static String sdReadText(const char* path) {
  if (!sdOk) return "";
  File f = SD.open(path, FILE_READ);
  if (!f) return "";
  String out = f.readString();
  f.close();
  return out;
}

static void sdCacheWrite(const char* name, const String& s) {
  if (!sdOk) return;
  sdEnsureDir(CACHE_DIR);
  String p = String(CACHE_DIR) + "/" + name;
  sdWriteText(p.c_str(), s);
}

static String sdCacheRead(const char* name) {
  if (!sdOk) return "";
  String p = String(CACHE_DIR) + "/" + name;
  return sdReadText(p.c_str());
}

static String cacheShowFileName(const String& showName) {
  String n = showName;
  n.replace("/", "_");
  n.replace("..", "_");
  return String("show_") + n + ".json";
}

// ---------------------- LVGL flush/touch ----------------------
static void lvgl_flush_cb(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* color_p) {
  if (!gfx) { lv_disp_flush_ready(disp); return; }
  const int32_t w = area->x2 - area->x1 + 1;
  const int32_t h = area->y2 - area->y1 + 1;
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t*)color_p, w, h);
  lv_disp_flush_ready(disp);
}

static void lvgl_touch_read_cb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
  (void)drv;
  if (!touch) {
    data->state = LV_INDEV_STATE_REL;
    return;
  }
  touch->read();
  if (touch->isTouched) {
    // GT911 reports x/y in screen coordinates for this module
    data->state = LV_INDEV_STATE_PR;
    data->point.x = touch->points[0].x;
    data->point.y = touch->points[0].y;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

// ---------------------- UI ----------------------
static void onPlay(lv_event_t* e) {
  (void)e;
  const char* n = lv_textarea_get_text(txtShowName);
  String url = String(BRAIN_HOST) + "/api/play?name=" + String(n ? n : "");
  (void)httpPostEmpty(url);
}

static void onStop(lv_event_t* e) {
  (void)e;
  String url = String(BRAIN_HOST) + "/api/stop";
  (void)httpPostEmpty(url);
}

static void editorMsg(const String& s) {
  if (!lblEditorMsg) return;
  lv_label_set_text(lblEditorMsg, s.c_str());
}

static void onLoadShow(lv_event_t* e) {
  (void)e;
  if (!taShowJson || !txtShowName) return;
  String name = String(lv_textarea_get_text(txtShowName));
  name.trim();
  if (!name.length()) { editorMsg("Enter show name"); return; }

  editorMsg("Loading...");
  yield();
  String body = httpGetText(String(BRAIN_HOST) + "/api/show?name=" + name, 900);

  if (!body.length()) {
    // Fallback to SD cache
    if (sdOk) {
      String cached = sdCacheRead(cacheShowFileName(name).c_str());
      if (cached.length()) {
        lv_textarea_set_text(taShowJson, cached.c_str());
        editorMsg("Loaded from SD cache");
        return;
      }
    }
    editorMsg("Load failed");
    return;
  }

  lv_textarea_set_text(taShowJson, body.c_str());
  if (sdOk) sdCacheWrite(cacheShowFileName(name).c_str(), body);
  editorMsg("Loaded");
}

static void onSaveShow(lv_event_t* e) {
  (void)e;
  if (!taShowJson || !txtShowName) return;
  String name = String(lv_textarea_get_text(txtShowName));
  name.trim();
  if (!name.length()) { editorMsg("Enter show name"); return; }

  String body = String(lv_textarea_get_text(taShowJson));
  body.trim();
  if (!body.length()) { editorMsg("Show JSON empty"); return; }

  editorMsg("Saving...");
  yield();

  bool ok = false;
  if (WiFi.status() == WL_CONNECTED) {
    ok = httpPostText(String(BRAIN_HOST) + "/api/show?name=" + name, body, "application/json", 1400);
  }

  if (ok) {
    if (sdOk) sdCacheWrite(cacheShowFileName(name).c_str(), body);
    editorMsg("Saved to Brain");
  } else {
    // Save to SD outbox as safety
    if (sdOk) {
      sdEnsureDir(OUTBOX_DIR);
      String path = String(OUTBOX_DIR) + "/" + cacheShowFileName(name);
      sdWriteText(path.c_str(), body);
      sdCacheWrite(cacheShowFileName(name).c_str(), body);
      editorMsg("Save failed; saved to SD outbox");
    } else {
      editorMsg("Save failed");
    }
  }
}

static void buildUI() {
  lv_obj_t* scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(CLR_GORE_BLACK), 0);

  // Top bar
  lv_obj_t* top = lv_obj_create(scr);
  lv_obj_set_size(top, lv_pct(100), 64);
  lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 0);

  txtShowName = lv_textarea_create(top);
  lv_obj_set_size(txtShowName, 220, 44);
  lv_obj_align(txtShowName, LV_ALIGN_LEFT_MID, 10, 0);
  lv_textarea_set_one_line(txtShowName, true);
  lv_textarea_set_text(txtShowName, "Haunt1");

  lv_obj_t* btnLoad = lv_btn_create(top);
  lv_obj_set_size(btnLoad, 110, 44);
  lv_obj_align(btnLoad, LV_ALIGN_LEFT_MID, 240, 0);
  lv_obj_add_event_cb(btnLoad, onLoadShow, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lblL = lv_label_create(btnLoad);
  lv_label_set_text(lblL, "LOAD");
  lv_obj_center(lblL);

  lv_obj_t* btnSave = lv_btn_create(top);
  lv_obj_set_size(btnSave, 110, 44);
  lv_obj_align(btnSave, LV_ALIGN_LEFT_MID, 360, 0);
  lv_obj_add_event_cb(btnSave, onSaveShow, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lblSv = lv_label_create(btnSave);
  lv_label_set_text(lblSv, "SAVE");
  lv_obj_center(lblSv);

  lv_obj_t* btnPlay = lv_btn_create(top);
  lv_obj_set_size(btnPlay, 110, 44);
  lv_obj_align(btnPlay, LV_ALIGN_LEFT_MID, 480, 0);
  lv_obj_add_event_cb(btnPlay, onPlay, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lblP = lv_label_create(btnPlay);
  lv_label_set_text(lblP, "PLAY");
  lv_obj_center(lblP);

  lv_obj_t* btnStop = lv_btn_create(top);
  lv_obj_set_size(btnStop, 110, 44);
  lv_obj_align(btnStop, LV_ALIGN_LEFT_MID, 600, 0);
  lv_obj_add_event_cb(btnStop, onStop, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lblS = lv_label_create(btnStop);
  lv_label_set_text(lblS, "STOP");
  lv_obj_center(lblS);

  lblEditorMsg = lv_label_create(top);
  lv_obj_align(lblEditorMsg, LV_ALIGN_RIGHT_MID, -10, 0);
  lv_label_set_text(lblEditorMsg, "");

  // Tabs
  lv_obj_t* tabs = lv_tabview_create(scr, LV_DIR_TOP, 48);
  lv_obj_set_size(tabs, SCREEN_W, SCREEN_H - 64);
  lv_obj_align(tabs, LV_ALIGN_BOTTOM_MID, 0, 0);

  lv_obj_t* tabMonitor = lv_tabview_add_tab(tabs, "Monitor");
  lv_obj_t* tabEditor  = lv_tabview_add_tab(tabs, "Editor");

  // Monitor tab layout (3 panels)
  lv_obj_t* body = lv_obj_create(tabMonitor);
  lv_obj_set_size(body, lv_pct(100), lv_pct(100));
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);

  auto makePanel = [&](const char* title) -> lv_obj_t* {
    lv_obj_t* p = lv_obj_create(body);
    lv_obj_set_size(p, SCREEN_W / 3 - 8, SCREEN_H - 90);
    lv_obj_t* t = lv_label_create(p);
    lv_label_set_text(t, title);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 8, 6);
    return p;
  };

  lv_obj_t* p1 = makePanel("STATUS");
  lblStatus = lv_label_create(p1);
  lv_obj_align(lblStatus, LV_ALIGN_TOP_LEFT, 8, 32);
  lv_label_set_long_mode(lblStatus, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lblStatus, lv_pct(100));

  lv_obj_t* p2 = makePanel("PROPS");
  lblProps = lv_label_create(p2);
  lv_obj_align(lblProps, LV_ALIGN_TOP_LEFT, 8, 32);
  lv_label_set_long_mode(lblProps, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lblProps, lv_pct(100));

  lv_obj_t* p3 = makePanel("SHOWS");
  lblShows = lv_label_create(p3);
  lv_obj_align(lblShows, LV_ALIGN_TOP_LEFT, 8, 32);
  lv_label_set_long_mode(lblShows, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lblShows, lv_pct(100));

  // Editor tab: show JSON editor
  lv_obj_t* ed = lv_obj_create(tabEditor);
  lv_obj_set_size(ed, lv_pct(100), lv_pct(100));
  lv_obj_set_style_pad_all(ed, 8, 0);

  taShowJson = lv_textarea_create(ed);
  lv_obj_set_size(taShowJson, lv_pct(100), lv_pct(100));
  lv_textarea_set_text(taShowJson,
    "{\"name\":\"Haunt1\",\"events\":[\\n"
    "  {\"t\":0,\"type\":\"prop\",\"targets\":[\"LanternA\"],\"payload\":{\"cmd\":\"mp3\",\"action\":\"play\",\"track\":3,\"volume\":20}},\\n"
    "  {\"t\":0,\"type\":\"wled\",\"host\":\"192.168.1.50\",\"state\":{\"on\":true,\"ps\":1}},\\n"
    "  {\"t\":5000,\"type\":\"prop\",\"targets\":[\"LanternA\"],\"payload\":{\"cmd\":\"mp3\",\"action\":\"stop\"}}\\n"
    "]}");
}

// ---------------------- Init: display/touch/sd/wifi ----------------------
static void initDisplay() {
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  rgbPanel = new Arduino_ESP32RGBPanel(
    TFT_DE, TFT_VSYNC, TFT_HSYNC, TFT_PCLK,
    TFT_R1, TFT_R2, TFT_R3, TFT_R4, TFT_R5,
    TFT_G0, TFT_G1, TFT_G2, TFT_G3, TFT_G4, TFT_G5,
    TFT_B1, TFT_B2, TFT_B3, TFT_B4, TFT_B5
  );

  gfx = new Arduino_RGB_Display(
    SCREEN_W, SCREEN_H, rgbPanel,
    0 /* rotation */, true /* auto_flush */
  );

  gfx->begin();
  gfx->fillScreen(0x0000);
}

static void initTouch() {
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  touch = new TAMC_GT911(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, SCREEN_W, SCREEN_H);
  touch->begin();
}

static void initSD() {
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  sdOk = SD.begin(SD_CS_PIN);
  if (sdOk) {
    sdEnsureDir(CACHE_DIR);
    sdEnsureDir(OUTBOX_DIR);
  }
}

static void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

static void initLVGL() {
  lv_init();

  lv_color_t* bufA = (lv_color_t*)heap_caps_malloc(DRAW_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  lv_color_t* bufB = (lv_color_t*)heap_caps_malloc(DRAW_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!bufA || !bufB) {
    // fallback: smaller buffer
    const uint32_t smallSz = SCREEN_W * 20;
    bufA = (lv_color_t*)malloc(smallSz * sizeof(lv_color_t));
    bufB = (lv_color_t*)malloc(smallSz * sizeof(lv_color_t));
    lv_disp_draw_buf_init(&draw_buf, bufA, bufB, smallSz);
  } else {
    lv_disp_draw_buf_init(&draw_buf, bufA, bufB, DRAW_BUF_SIZE);
  }

  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = SCREEN_W;
  disp_drv.ver_res = SCREEN_H;
  disp_drv.flush_cb = lvgl_flush_cb;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = lvgl_touch_read_cb;
  indev = lv_indev_drv_register(&indev_drv);
}

// ---------------------- Poll loop ----------------------
static uint32_t lastStatus = 0;
static uint32_t lastProps = 0;
static uint32_t lastShows = 0;

static void pollBrain() {
  const uint32_t now = millis();
  if (WiFi.status() != WL_CONNECTED) return;

  if (now - lastStatus >= POLL_STATUS_MS) {
    lastStatus = now;
    String s = httpGetText(String(BRAIN_HOST) + "/api/status");
    if (s.length()) { uiSetLabel(lblStatus, s); sdCacheWrite("status.json", s); }
  }
  if (now - lastProps >= POLL_PROPS_MS) {
    lastProps = now;
    String s = httpGetText(String(BRAIN_HOST) + "/api/props");
    if (s.length()) { uiSetLabel(lblProps, s); sdCacheWrite("props.json", s); }
  }
  if (now - lastShows >= POLL_SHOWS_MS) {
    lastShows = now;
    String s = httpGetText(String(BRAIN_HOST) + "/api/shows");
    if (s.length()) { uiSetLabel(lblShows, s); sdCacheWrite("shows.json", s); }
  }
}

void setup() {
  Serial.begin(115200);
  delay(50);

  esp_task_wdt_init(5, true);
  esp_task_wdt_add(NULL);

  initDisplay();
  initTouch();
  initSD();
  initLVGL();
  buildUI();

  // Load cached content (so UI isn't blank before Wi-Fi comes up)
  if (sdOk) {
    String s;
    s = sdCacheRead("status.json"); if (s.length()) uiSetLabel(lblStatus, s);
    s = sdCacheRead("props.json");  if (s.length()) uiSetLabel(lblProps, s);
    s = sdCacheRead("shows.json");  if (s.length()) uiSetLabel(lblShows, s);
    // Load last cached show draft for default name
    String name = String(lv_textarea_get_text(txtShowName));
    name.trim();
    if (name.length() && taShowJson) {
      s = sdCacheRead(cacheShowFileName(name).c_str());
      if (s.length()) lv_textarea_set_text(taShowJson, s.c_str());
    }
  }

  initWiFi();
}

void loop() {
  // LVGL first (keep UI responsive)
  lv_timer_handler();
  pollBrain();

  // Keep watchdog happy and prevent long-blocking
  esp_task_wdt_reset();
  yield();
  delay(5);
}
