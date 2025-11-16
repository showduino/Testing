#include "audio.h"

#include <SerialMP3Player.h>
#include <SoftwareSerial.h>

#include "config.h"
#include "duoframe.h"

namespace {

SerialMP3Player ambientPlayer(MP3_AMBIENT_RX, MP3_AMBIENT_TX);
SerialMP3Player machinePlayer(MP3_MACHINE_RX, MP3_MACHINE_TX);

constexpr uint8_t kMaxVolume = 30;

uint8_t ambientVolume = 20;
uint8_t machineVolume = 20;
uint8_t ambientTrack = 0;
uint8_t machineTrack = 0;

// Track metadata extracted from legacy sketch comments
const char *const ambientTrackNames[] = {
    "unused",
    "Victorian London",
    "Machine Ambience",
    "Guns",
    "Warning",
    "Tension",
    "Rainforest",
    "Electric",
    "Machine Travelling",
    "Machine Break",
    "Radio 1",
    "Radio 2",
    "Radio 3",
    "Radio 4",
    "Radio 5",
    "Radio 6",
    "Radio 7",
    "Radio 8",
    "Radio 9",
    "Radio 10",
    "Dinosaur Roar",
    "Machine Boot Up",
    "Whales"};

const char *const machineTrackNames[] = {
    "unused",
    "Machine Ambience",
    "Victorian London",
    "Whales",
    "Reserved",
    "Machine Boot Up",
    "Dino Roar",
    "Radio - Power Back",
    "Radio - Losing Control",
    "Radio - Paradox Lock Fail",
    "Radio - 5010 Pressure",
    "Radio - 0000",
    "Radio - Close Blast Shield",
    "Radio - Remote Control",
    "Radio - Remain In Contact",
    "Machine Fail",
    "Travelling",
    "Guns",
    "Warning",
    "Electric",
    "Rainforest",
    "Tension",
    "Reserved"};

enum AudioAction : uint8_t {
  AUDIO_PLAY = 0,
  AUDIO_STOP = 1,
  AUDIO_VOLUME = 2,
};

void sendAck(char player, uint8_t action, uint8_t value, uint8_t status) {
  uint8_t payload[4] = {static_cast<uint8_t>(player), action, value, status};
  duoframe::send(showduino::DF_CMD_AUDIO, payload, sizeof(payload));
}

SerialMP3Player &playerFor(char id) {
  return (id == 'A') ? ambientPlayer : machinePlayer;
}

void playTrack(char playerId, uint8_t track) {
  if (track == 0) return;
  SerialMP3Player &playerRef = playerFor(playerId);
  playerRef.play(track);
  if (playerId == 'A') {
    ambientTrack = track;
  } else {
    machineTrack = track;
  }
}

void stopTrack(char playerId) {
  SerialMP3Player &playerRef = playerFor(playerId);
  playerRef.stop();
  if (playerId == 'A') {
    ambientTrack = 0;
  } else {
    machineTrack = 0;
  }
}

void setVolume(char playerId, uint8_t vol) {
  vol = constrain(vol, (uint8_t)0, kMaxVolume);
  SerialMP3Player &playerRef = playerFor(playerId);
  playerRef.volume(vol);
  if (playerId == 'A') {
    ambientVolume = vol;
  } else {
    machineVolume = vol;
  }
}

}  // namespace

void audio_begin() {
  ambientPlayer.begin(MP3_SERIAL_BAUD);
  machinePlayer.begin(MP3_SERIAL_BAUD);
  setVolume('A', ambientVolume);
  setVolume('B', machineVolume);
}

void audio_update() {
  // Placeholder for future status polling or watchdog logic
}

void audio_handleCommand(const showduino::DuoFrame &frame) {
  if (frame.length < 2) return;
  char player = static_cast<char>(frame.payload[0]);

  if (frame.length == 2) {
    uint8_t track = frame.payload[1];
    playTrack(player, track);
    sendAck(player, AUDIO_PLAY, track, 0);
    return;
  }

  uint8_t action = frame.payload[1];
  uint8_t value = frame.length > 2 ? frame.payload[2] : 0;

  switch (action) {
    case AUDIO_PLAY:
      playTrack(player, value);
      break;
    case AUDIO_STOP:
      stopTrack(player);
      break;
    case AUDIO_VOLUME:
      setVolume(player, value);
      break;
    default:
      break;
  }

  sendAck(player, action, value, 0);
}

void audio_playAmbientTrack(uint8_t track) { playTrack('A', track); }

void audio_playMachineTrack(uint8_t track) { playTrack('B', track); }

void audio_stopAmbient() { stopTrack('A'); }

void audio_stopMachine() { stopTrack('B'); }

void audio_setAmbientVolume(uint8_t volume) { setVolume('A', volume); }

void audio_setMachineVolume(uint8_t volume) { setVolume('B', volume); }

uint8_t audio_currentAmbientTrack() { return ambientTrack; }

uint8_t audio_currentMachineTrack() { return machineTrack; }
