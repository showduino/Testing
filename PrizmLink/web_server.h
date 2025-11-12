#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include "config.h"

namespace WebServer {

bool begin(const Prizm::PrizmConfig &cfg);
void loop(const Prizm::RuntimeStats &stats);
void broadcastStatus(const Prizm::RuntimeStats &stats);

} // namespace WebServer

