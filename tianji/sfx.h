// Synthesised sound effects (wood knocks, dice clacks, coin rings) played through the speaker.
#pragma once
#include <Arduino.h>

void sfxInit();
void sfxRelease();  // free the large buffers (e.g. while Bluetooth needs the RAM)
void sfxTick(float pitch = 1.0f, float vol = 1.0f);
void sfxKnock(float pitch = 1.0f, float vol = 1.0f);   // wooden block / bamboo
void sfxClack(float pitch = 1.0f, float vol = 1.0f);   // dice on a table
void sfxRing(float pitch = 1.0f, float vol = 1.0f);    // bronze coin
void sfxChime(float pitch = 1.0f, float vol = 1.0f);   // soft bell for a revealed result
void sfxWhoosh(float vol = 1.0f);
void sfxBeep(float freq, uint32_t ms);
bool sfxAvailable();
