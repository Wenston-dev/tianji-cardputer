// Long-form WAV recording to SD: a capture task feeds a ring of buffers, a writer task streams
// them to the card and refreshes the WAV header every few seconds so a power cut loses little.
#pragma once
#include <Arduino.h>
#include <FS.h>

namespace rec {

enum State { IDLE, RECORDING, PAUSED, STOPPING };

struct Preset {
  const char* name;
  const char* desc;
  int8_t rateIdx;   // -1 = use settings
  uint16_t offSec;  // screen-off delay, 0xFFFF = use settings
};
extern const Preset kPresets[3];

bool sdBegin();
bool sdOk();
uint64_t sdFreeBytes();
void sdLock();
void sdUnlock();
fs::FS& fs();

bool start(int preset);
void stop();
void togglePause();
void mark();
// Main-loop housekeeping: finishes a stop (hands the codec back to the speaker).
void service();

State state();
uint32_t elapsedMs();
uint64_t dataBytes();
uint32_t sampleRate();
const char* path();
int presetIdx();
uint16_t presetOffSec();
uint32_t overruns();
int markCount();
const char* lastError();
// Consume the "a recording just finished" notification.
bool takeFinished(char* pathOut, size_t n, uint32_t* ms);

// Visuals: level history (0..255, dB-scaled peaks, ~31 ms per entry), oldest first.
int history(uint8_t* out, int n);
// Absolute-index access so the UI can scroll smoothly between entries.
uint32_t historyTotal();
uint8_t historyAt(uint32_t idx);  // 0 if no longer kept
float historyRate();              // entries per second
float peakLevel();  // 0..1 smoothed, dB-scaled
int scope(int16_t* out, int n);

// WAV helpers shared with the player.
struct WavInfo {
  uint32_t rate;
  uint16_t channels, bits;
  uint32_t dataOffset, dataBytes;
};
bool readWavInfo(File& f, WavInfo& w);

}  // namespace rec
