#include "config.h"
#include "sd_logger.h"
#include "debug_utils.h"
#include <ctime>

namespace SDLogger {

static Options sOpts {};
static fs::FS *sFS = nullptr;
static String sCurrentPath;
static File sFile;
static bool sReady = false;

static String dateStamp() {
  time_t now = time(nullptr);
  struct tm timeinfo;
  if (!localtime_r(&now, &timeinfo)) {
    return "1970-01-01";
  }
  char buf[16];
  strftime(buf, sizeof(buf), "%Y-%m-%d", &timeinfo);
  return String(buf);
}

static void ensureDirectory(const String &path) {
  if (!sFS) return;
  if (!sFS->exists(path)) {
    sFS->mkdir(path);
  }
}

static fs::FS &getFs() {
  return *sFS;
}

bool begin(const Options &opts) {
  sOpts = opts;
  if (!opts.enabled) {
    sReady = false;
    return false;
  }

  if (!SD.begin(opts.csPin)) {
    Debug::warn("SD", "SD.begin failed (CS=%d)", opts.csPin);
    sReady = false;
    return false;
  }

  sFS = &SD;
  ensureDirectory("/logs");
  ensureDirectory("/fx");
  sReady = true;

  String stamp = dateStamp();
  sCurrentPath = opts.bootPrefix + stamp + ".txt";
  sFile = getFs().open(sCurrentPath, FILE_APPEND);
  if (!sFile) {
    Debug::error("SD", "Failed to open log %s", sCurrentPath.c_str());
    sReady = false;
    return false;
  }

  append("=== PrizmLink boot log ===");
  append(String("Firmware ") + Prizm::kFirmwareVersion);

  return true;
}

void end() {
  if (sFile) {
    sFile.flush();
    sFile.close();
  }
  if (sFS) {
    SD.end();
  }
  sFile = File();
  sFS = nullptr;
  sReady = false;
}

void rotateIfNeeded() {
  if (!sReady || !sFile) return;
  if (sFile.size() < sOpts.maxFileSize) return;

  sFile.flush();
  sFile.close();

  String stamp = dateStamp();
  sCurrentPath = sOpts.runPrefix + stamp + ".txt";
  sFile = getFs().open(sCurrentPath, FILE_WRITE);
  if (!sFile) {
    Debug::error("SD", "Log rotate open failed %s", sCurrentPath.c_str());
    return;
  }
  append("=== Rotated log ===");
}

void append(const char *line) {
  if (!sReady) return;

  if (sFile) {
    sFile.println(line);
    if (sFile.size() % 4096 == 0) sFile.flush();
    rotateIfNeeded();
  }
}

void append(const String &line) {
  append(line.c_str());
}

void flush() {
  if (sFile) sFile.flush();
}

String currentLogPath() {
  return sCurrentPath;
}

bool isReady() {
  return sReady;
}

} // namespace SDLogger

