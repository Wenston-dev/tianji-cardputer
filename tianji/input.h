// Keyboard events with edge detection, key repeat, the G0 button and IMU shake.
#pragma once
#include <Arduino.h>

enum : uint16_t {
  K_ENTER = 0x100,
  K_BACK,
  K_TAB,
  K_ESC,
  K_UP,
  K_DOWN,
  K_LEFT,
  K_RIGHT,
  K_G0,
};

struct KeyEvent {
  uint16_t key;  // lowercase ASCII or one of the K_* codes
  char ch;       // printable character (shift applied), 0 if none
  bool shift, fn, ctrl, alt, opt, repeat;
};

void inputInit();
void inputPoll();
bool inputNext(KeyEvent& e);
bool keyDown(uint16_t key);  // is this key physically held right now
void inputTextMode(bool on);  // in text mode ` , . ; / type characters unless Fn is held
void inputFlush();

float g0Held();  // seconds G0 has been held (0 when up)
float shakeLevel();
bool imuReady();
