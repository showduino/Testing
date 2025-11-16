#include "audio.h"

#include "duoframe.h"

namespace {

struct AudioState {
  uint8_t volumeA = 180;
  uint8_t volumeB = 180;
} audioState;

}  // namespace

void audio_begin() {
  // Configure serial interfaces to YX5300 players here when available
}

void audio_handleCommand(const showduino::DuoFrame &frame) {
  if (frame.length < 2) return;
  char player = static_cast<char>(frame.payload[0]);
  uint8_t value = frame.payload[1];

  if (player == 'A') {
    audioState.volumeA = value;
  } else if (player == 'B') {
    audioState.volumeB = value;
  }

  // Stub: respond with acknowledgment
  uint8_t payload[3] = {static_cast<uint8_t>(player), value, 1};
  duoframe::send(showduino::DF_CMD_AUDIO, payload, sizeof(payload));
}
