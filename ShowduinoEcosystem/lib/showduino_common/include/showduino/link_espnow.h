#pragma once

#include <Arduino.h>

#if defined(ESP32)
#include <esp_now.h>
#include <WiFi.h>
#endif

namespace showduino {

#if defined(ESP32)

using EspNowRecvFn = void (*)(const uint8_t *mac, const uint8_t *data, int len, void *user);

class EspNowLink {
 public:
  bool begin(EspNowRecvFn cb, void *user);
  bool addPeer(const uint8_t mac[6]);
  bool addBroadcastPeer();
  bool send(const uint8_t mac[6], const uint8_t *data, size_t len);

 private:
  static void on_recv(const uint8_t *mac, const uint8_t *data, int len);
  static EspNowLink *self_;

  EspNowRecvFn cb_ = nullptr;
  void *user_ = nullptr;
};

#endif  // ESP32

}  // namespace showduino

