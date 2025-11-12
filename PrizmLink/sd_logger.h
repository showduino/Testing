#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>

namespace SDLogger {

struct Options {
  bool enabled {true};
  uint8_t csPin {10};
  bool preferSDMMC {false};
  String bootPrefix {"/logs/boot_"};
  String runPrefix {"/logs/run_"};
  size_t maxFileSize {64 * 1024};
};

bool begin(const Options &opts);
void end();

void append(const String &line);
void append(const char *line);
void flush();

String currentLogPath();
void rotateIfNeeded();

bool isReady();

} // namespace SDLogger

