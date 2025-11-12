#include <vector>
#include <esp32/rom/ets_sys.h>
#include <cstring>
#include "dmx_output.h"
#include "debug_utils.h"
#include <driver/uart.h>

namespace DMXOutput {

static const uart_port_t kDMXPort = UART_NUM_1;
static bool sReady = false;
static uint16_t sChannelCount = 0;
static uint32_t sFrameIntervalUs = 25000; // 40 FPS default
static uint32_t sLastFrameUs = 0;
static std::vector<uint8_t> sBuffer;

bool begin(const Prizm::DMXConfig &cfg) {
  if (!cfg.enabled) {
    Debug::warn("DMX", "Disabled via config");
    sReady = false;
    return false;
  }

  uart_config_t uart_config = {
      .baud_rate = 250000,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_NONE,
      .stop_bits = UART_STOP_BITS_2,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_APB,
  };

  if (uart_param_config(kDMXPort, &uart_config) != ESP_OK) {
    Debug::error("DMX", "uart_param_config failed");
    return false;
  }

  if (uart_set_pin(kDMXPort, cfg.txPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
    Debug::error("DMX", "uart_set_pin failed");
    return false;
  }

  if (uart_driver_install(kDMXPort, 1024, 0, 0, nullptr, 0) != ESP_OK) {
    Debug::error("DMX", "uart_driver_install failed");
    return false;
  }

  sChannelCount = std::min<uint16_t>(cfg.channels, 512);
  sBuffer.assign(sChannelCount + 1, 0); // start code + payload
  sFrameIntervalUs = 1000000UL / std::max<uint16_t>(cfg.fps, 1);
  sLastFrameUs = esp_timer_get_time();
  sReady = true;

  Debug::info("DMX", "Started (%u channels @ %u FPS)", sChannelCount, cfg.fps);
  return true;
}

void update(const uint8_t *data, size_t length) {
  if (!sReady) return;
  if (length > sChannelCount) length = sChannelCount;
  memcpy(sBuffer.data() + 1, data, length);
}

void loop() {
  if (!sReady) return;
  uint64_t now = esp_timer_get_time();
  if (now - sLastFrameUs < sFrameIntervalUs) return;
  sLastFrameUs = now;

  uart_write_bytes_with_break(kDMXPort,
                              reinterpret_cast<const char*>(sBuffer.data()),
                              sChannelCount + 1,
                              100); // 100us break
}

void blackout() {
  if (!sReady) return;
  memset(sBuffer.data() + 1, 0, sChannelCount);
  uart_write_bytes_with_break(kDMXPort,
                              reinterpret_cast<const char*>(sBuffer.data()),
                              sChannelCount + 1,
                              100);
}

bool isReady() {
  return sReady;
}

} // namespace DMXOutput

