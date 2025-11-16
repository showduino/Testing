#include "dmx_scene.h"

namespace {

uint32_t currentPosition = 0;
constexpr uint32_t kDefaultLength = 180000;

}  // namespace

void dmx_scene_begin() {
  currentPosition = 0;
}

void dmx_scene_seek(uint32_t positionMs) {
  currentPosition = positionMs;
}

uint32_t dmx_scene_length() {
  return kDefaultLength;
}
