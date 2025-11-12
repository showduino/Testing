#include <Wire.h>
#include <WiFi.h>
#include "oled_display.h"
#include "debug_utils.h"

namespace OLEDDisplay {

static Adafruit_SSD1306 sDisplay(128, 32, &Wire, -1);
static bool sReady = false;

bool begin(const Prizm::OLEDConfig &cfg) {
  if (!cfg.enabled) {
    sReady = false;
    return false;
  }
  Wire.begin(cfg.sda, cfg.scl, 400000);
  if (!sDisplay.begin(SSD1306_SWITCHCAPVCC, cfg.address)) {
    Debug::error("OLED", "SSD1306 not found at 0x%02X", cfg.address);
    sReady = false;
    return false;
  }
  sDisplay.clearDisplay();
  sDisplay.setTextSize(1);
  sDisplay.setTextColor(SSD1306_WHITE);
  sDisplay.setCursor(0, 0);
  sDisplay.println("PrizmLink");
  sDisplay.display();
  sReady = true;
  return true;
}

static void drawLine(const char *label, const String &value, int row) {
  sDisplay.setCursor(0, row * 8);
  sDisplay.print(label);
  sDisplay.print(": ");
  sDisplay.print(value);
}

void update(const Prizm::RuntimeStats &stats, const Prizm::PrizmConfig &cfg) {
  if (!sReady) return;
  sDisplay.clearDisplay();

  drawLine("IP", WiFi.localIP().toString(), 0);
  drawLine("FPS", String(stats.fps, 1), 1);
  drawLine("DMX", String(cfg.dmx.channels), 2);
  drawLine("Px", String(cfg.pixels.count), 3);
  sDisplay.display();
}

void showEmergency() {
  if (!sReady) return;
  sDisplay.clearDisplay();
  sDisplay.setCursor(0, 8);
  sDisplay.setTextSize(1);
  sDisplay.println("EMERGENCY STOP");
  sDisplay.println("Outputs disabled");
  sDisplay.display();
}

} // namespace OLEDDisplay

