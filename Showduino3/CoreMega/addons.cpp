#include "addons.h"

#include <Wire.h>

#include "config.h"
#include "duoframe.h"

namespace {

bool addonsAnnounced = false;

void broadcastDefaultProfile() {
  const char payload[] =
      "{\"addons\":[{\"id\":\"relay8\",\"type\":\"relay\",\"channels\":8},"
      "{\"id\":\"buttons32\",\"type\":\"input\",\"channels\":32}]}";
  duoframe::send(showduino::DF_CMD_ADDON_LIST,
                 reinterpret_cast<const uint8_t *>(payload),
                 sizeof(payload) - 1);
}

}  // namespace

void addons_begin() {
  Wire.begin(ADDON_I2C_SDA, ADDON_I2C_SCL);
  Wire.setClock(ADDON_I2C_FREQ);
}

void addons_update() {
  if (!addonsAnnounced && millis() > 2000) {
    broadcastDefaultProfile();
    addonsAnnounced = true;
  }
}

void addons_handleRelay(uint8_t relayIndex, uint8_t state) {
  (void)relayIndex;
  (void)state;
  // Future: delegate to SX1509 relay boards
}
