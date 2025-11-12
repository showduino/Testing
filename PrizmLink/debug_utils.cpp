#include "debug_utils.h"
#include "sd_logger.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace Debug {

static Level sMinimum = Level::Info;
static bool sSerialMirror = true;
static bool sSDMirror = true;
static SemaphoreHandle_t sMutex = nullptr;

static const char *levelToString(Level level) {
  switch (level) {
    case Level::Verbose: return "VERBOSE";
    case Level::Info:    return "INFO";
    case Level::Warn:    return "WARN";
    case Level::Error:   return "ERROR";
  }
  return "UNK";
}

void begin(Level minimum, bool mirrorSerial) {
  sMinimum = minimum;
  sSerialMirror = mirrorSerial;
  if (!sMutex) {
    sMutex = xSemaphoreCreateMutex();
  }
}

void setMinimum(Level level) {
  sMinimum = level;
}

Level minimum() {
  return sMinimum;
}

void setSDMirror(bool enabled) {
  sSDMirror = enabled;
}

void vlog(Level level, const char *tag, const char *fmt, va_list args) {
  if (level < sMinimum) return;
  if (!sMutex) begin(sMinimum, sSerialMirror);
  if (!sMutex) return;

  if (xSemaphoreTake(sMutex, pdMS_TO_TICKS(50)) != pdTRUE) return;

  char buffer[256];
  va_list argsCopy;
  va_copy(argsCopy, args);
  vsnprintf(buffer, sizeof(buffer), fmt, argsCopy);
  va_end(argsCopy);

  uint32_t ms = millis();
  char line[320];
  snprintf(line, sizeof(line), "[%lu.%03lu][%s][%s] %s",
           ms / 1000, ms % 1000, levelToString(level), tag, buffer);

  if (sSerialMirror) {
    Serial.println(line);
  }

  if (sSDMirror) {
    SDLogger::append(line);
  }

  xSemaphoreGive(sMutex);
}

void log(Level level, const char *tag, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vlog(level, tag, fmt, args);
  va_end(args);
}

} // namespace Debug

