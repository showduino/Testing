#pragma once

#include <Arduino.h>
#include <cstdarg>

namespace Debug {

enum class Level : uint8_t {
  Verbose = 0,
  Info,
  Warn,
  Error
};

void begin(Level minimum = Level::Info, bool mirrorSerial = true);
void setMinimum(Level level);
Level minimum();

void log(Level level, const char *tag, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
void vlog(Level level, const char *tag, const char *fmt, va_list args);

inline void verbose(const char *tag, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
inline void info(const char *tag, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
inline void warn(const char *tag, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
inline void error(const char *tag, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

void setSDMirror(bool enabled);

} // namespace Debug

// ────────────────────────────────────────────────────────────────
//  INLINE IMPLEMENTATIONS
// ────────────────────────────────────────────────────────────────
inline void Debug::verbose(const char *tag, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  Debug::vlog(Level::Verbose, tag, fmt, args);
  va_end(args);
}

inline void Debug::info(const char *tag, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  Debug::vlog(Level::Info, tag, fmt, args);
  va_end(args);
}

inline void Debug::warn(const char *tag, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  Debug::vlog(Level::Warn, tag, fmt, args);
  va_end(args);
}

inline void Debug::error(const char *tag, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  Debug::vlog(Level::Error, tag, fmt, args);
  va_end(args);
}

