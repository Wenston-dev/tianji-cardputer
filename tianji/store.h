// Recording library on the SD card: listing, deletion, waveform overviews, markers and playback.
#pragma once
#include <Arduino.h>
#include <vector>

namespace store {

struct Entry {
  char path[48];
  char title[32];
  uint32_t ms;
  uint32_t bytes;
  uint32_t rate;
};

int list(std::vector<Entry>& out);  // newest first
bool remove(const char* path);
void titleFor(const char* path, char* out, size_t n);

// Peak overview of a file, computed in the background (columns 0..255 each).
void overviewStart(const char* path, int columns);
int overviewDone();  // columns ready so far
const uint8_t* overview();
const char* overviewPath();

int loadMarks(const char* path, float* out, int max);

bool playOpen(const char* path);
void playClose();
void playToggle();
bool playing();
void playSeek(float sec);  // absolute
float playPos();
float playLen();
float playLevel();  // 0..1
void playService();  // keep the speaker queue fed; call every frame
void playVolume(int delta);
int playVolumeNow();

}  // namespace store
