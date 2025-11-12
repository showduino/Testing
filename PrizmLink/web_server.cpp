#include "web_server.h"
#include "debug_utils.h"
#include <WiFi.h>
#include "config.h"
#include "sd_logger.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <SD.h>

namespace WebServer {

static AsyncWebServer *sServer = nullptr;
static AsyncWebSocket *sSocket = nullptr;
static bool sReady = false;
static Prizm::PrizmConfig sCfg;

static String processor(const String &var) {
  if (var == "VERSION") return Prizm::kFirmwareVersion;
  if (var == "IP") return WiFi.localIP().toString();
  return String();
}

static void handleWsMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = static_cast<AwsFrameInfo *>(arg);
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    String msg = String(reinterpret_cast<char*>(data), len);
    Debug::info("WS", "Received: %s", msg.c_str());
  }
}

bool begin(const Prizm::PrizmConfig &cfg) {
  sCfg = cfg;
  uint16_t port = cfg.web.port;
  if (sServer) {
    delete sServer;
    sServer = nullptr;
  }
  if (sSocket) {
    delete sSocket;
    sSocket = nullptr;
  }
  sServer = new AsyncWebServer(port);
  sSocket = new AsyncWebSocket("/ws");

  if (cfg.web.enabled) {
    sSocket->onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
                        void *arg, uint8_t *data, size_t len) {
      switch (type) {
        case WS_EVT_CONNECT:
          Debug::info("WS", "Client connected (%u)", client->id());
          break;
        case WS_EVT_DISCONNECT:
          Debug::info("WS", "Client disconnected (%u)", client->id());
          break;
        case WS_EVT_DATA:
          handleWsMessage(arg, data, len);
          break;
        default:
          break;
      }
    });
    sServer->addHandler(sSocket);

    sServer->on("/config", HTTP_GET, [](AsyncWebServerRequest *request) {
      String json = Prizm::Config::toJsonString(Prizm::Config::active, true);
      request->send(200, "application/json", json);
    });

    sServer->on("/logs/run_latest.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
      if (!SDLogger::isReady()) {
        request->send(503, "text/plain", "SD logger unavailable");
        return;
      }
      String path = SDLogger::currentLogPath();
      if (path.isEmpty() || !SD.exists(path)) {
        request->send(404, "text/plain", "No log available");
        return;
      }
      request->send(SD, path, "text/plain");
    });

    sServer->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
      if (SDLogger::isReady() && SD.exists("/web/index.html")) {
        request->send(SD, "/web/index.html", "text/html", false, processor);
      } else if (LittleFS.begin(true) && LittleFS.exists("/web/index.html")) {
        request->send(LittleFS, "/web/index.html", "text/html", false, processor);
      } else {
        request->send(200, "text/plain", "PrizmLink WebUI not found");
      }
    });

    if (SDLogger::isReady()) {
      sServer->serveStatic("/web", SD, "/web/");
    } else if (LittleFS.begin(true)) {
      sServer->serveStatic("/web", LittleFS, "/web/");
    }
    sServer->begin();
    Debug::info("WEB", "AsyncWebServer started on port %u", port);
    sReady = true;
  }
  return sReady;
}

void broadcastStatus(const Prizm::RuntimeStats &stats) {
  if (!sReady || !sSocket) return;
  DynamicJsonDocument doc(1024);
  doc["fps"] = stats.fps;
  doc["packets"] = stats.packetCounter;
  doc["manual"] = stats.manualOverride;
  doc["uptime"] = millis();
  String json;
  serializeJson(doc, json);
  sSocket->textAll(json);
}

void loop(const Prizm::RuntimeStats &stats) {
  if (!sReady) return;
  if (millis() - Prizm::Config::stats.lastWebsocketMs > 1000) {
    broadcastStatus(stats);
    Prizm::Config::stats.lastWebsocketMs = millis();
  }
}

} // namespace WebServer

