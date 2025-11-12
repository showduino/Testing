# PrizmLink – ESP32-S3 E1.31 Lighting & Motion Receiver

PrizmLink turns an ESP32-S3 module into a combined sACN (E1.31) pixel, DMX512, and servo receiver with a local WebUI, OLED status display, manual overrides, and SD-based configuration.

## Module Overview

- `PrizmLink_E131.ino` – Entry point for Arduino, orchestrates setup/loop, schedules subsystems.
- `config.h` – Persistent configuration, defaults, SD read/write helpers, runtime state containers.
- `network_e131.h` – Wi-Fi bring-up, E1.31 packet receive loop, universe merging, loss detection.
- `pixel_output.h` – WS2812/SK6812 driver using FastLED; brightness scaling, test FX, failsafe blending.
- `dmx_output.h` – DMX512 transmission over UART; configurable footprint and refresh.
- `joystick_servo.h` – PCA9685 servo driver and joystick/manual override logic.
- `pot_control.h` – Slide pot sampling & filtering for brightness/speed overrides.
- `buttons.h` – Debounced emergency/test/confirm buttons with event callbacks.
- `oled_display.h` – SSD1306 telemetry renderer for FPS, universes, servo angles, and status.
- `web_server.h` – AsyncWebServer hosting `/web/` assets from SD (or SPIFFS fallback) plus WebSocket telemetry.
- `sd_logger.h` – SD card initialization, log file rotation, append helpers with Serial mirroring.
- `failsafe_fx.h` – Time-based fallback animations when network data is lost.
- `debug_utils.h` – Unified logging macros that feed Serial and SD logs with timestamps.

## SD Layout

```
/web/           # HTML/CSS/JS assets
/logs/          # boot_YYYY-MM-DD.txt, run_YYYY-MM-DD.txt
/config.json    # saved configuration
/fx/            # JSON effect presets
```

## Build Notes

- Platform: ESP32-S3 + Arduino core 3.x
- Libraries: `ArduinoJson`, `ESPAsyncWebServer`, `AsyncTCP`, `FastLED`, `Adafruit_SSD1306`, `Adafruit_GFX`, `Adafruit_PWMServoDriver`, `AsyncTCP`, `FS`, `SD`, `SPI`
- Configure `sdkconfig` / board menu for PSRAM and 8MB flash; enable PSRAM for AsyncWebServer buffers.
- Define FreeRTOS task watchdog thresholds appropriately if adding additional tasks.

## Roadmap

1. Networking bring-up and E1.31 parsing (unicast + multicast).
2. Pixel + DMX output synchronization tests at 40 FPS.
3. Servo calibration UI & joystick override tuning.
4. WebSocket telemetry streaming to WebUI dashboard.
5. Robust logging/diagnostics and remote OTA support.

