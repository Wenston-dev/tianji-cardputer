// Host stand-in for M5Cardputer/M5Unified: the display is an off-screen sprite.
#pragma once
#include "Arduino.h"
#include <M5GFX.h>
#include <vector>

struct Point2D_t { int x = 0; int y = 0; };
struct KeyValue_t { char value_first; char value_second; };

struct FakeDisplay : public lgfx::LGFX_Sprite {
  void setBrightness(uint8_t) {}
};
struct FakeSpeaker {
  bool isEnabled() { return true; } bool isRunning() { return true; }
  void setVolume(uint8_t) {} bool begin() { return true; } void end() {}
  template <class T> bool playRaw(const T*, size_t, uint32_t, bool = false, uint32_t = 1, int = -1, bool = false) { return true; }
  bool tone(float, uint32_t, int = -1, bool = true) { return true; }
  size_t isPlaying(uint8_t) { return 0; } void stop(uint8_t) {} void stop() {}
};
struct FakeImu { bool isEnabled() { return false; } };
struct FakeBtn { bool wasPressed() { return false; } };
struct FakeM5 {
  FakeDisplay Display;
  FakeSpeaker Speaker;
  FakeImu Imu;
  FakeBtn BtnA;
};
extern FakeM5 M5;
inline void sfxReleaseDummy() {}
