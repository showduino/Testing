#pragma once

#include <Arduino.h>

#include "../common/DuoFrame.h"

void audio_begin();
void audio_update();
void audio_handleCommand(const showduino::DuoFrame &frame);

void audio_playAmbientTrack(uint8_t track);
void audio_playMachineTrack(uint8_t track);
void audio_stopAmbient();
void audio_stopMachine();
void audio_setAmbientVolume(uint8_t volume);
void audio_setMachineVolume(uint8_t volume);
uint8_t audio_currentAmbientTrack();
uint8_t audio_currentMachineTrack();
