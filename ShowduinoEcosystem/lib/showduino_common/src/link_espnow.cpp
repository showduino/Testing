#include <showduino/link_espnow.h>

#if defined(ESP32)

namespace showduino {

EspNowLink *EspNowLink::self_ = nullptr;

bool EspNowLink::begin(EspNowRecvFn cb, void *user) {
  cb_ = cb;
  user_ = user;
  self_ = this;

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, true);

  if (esp_now_init() != ESP_OK) return false;
  esp_now_register_recv_cb(&EspNowLink::on_recv);
  return true;
}

bool EspNowLink::addPeer(const uint8_t mac[6]) {
  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = 0;
  peer.encrypt = false;
  return esp_now_add_peer(&peer) == ESP_OK || esp_now_is_peer_exist(mac);
}

bool EspNowLink::addBroadcastPeer() {
  const uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  return addPeer(bcast);
}

bool EspNowLink::send(const uint8_t mac[6], const uint8_t *data, size_t len) {
  return esp_now_send(mac, data, len) == ESP_OK;
}

void EspNowLink::on_recv(const uint8_t *mac, const uint8_t *data, int len) {
  if (!self_ || !self_->cb_) return;
  self_->cb_(mac, data, len, self_->user_);
}

}  // namespace showduino

#endif  // ESP32

