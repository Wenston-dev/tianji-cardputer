// Date/time picker overlay shared by the home screen and Settings.
// ←→ choose a field, ↑↓ change it (holding repeats), digits type straight into it,
// Enter sets the clock, W syncs over WiFi when one is configured, Esc cancels.
#pragma once
#include "gfx.h"
#include "input.h"

struct TimePicker {
  bool open = false;

  void begin();  // prefilled with the current clock, or the firmware build date
  // Returns true when the key was consumed. Closes itself on Enter / W / Esc.
  bool key(const KeyEvent& e);
  void draw(float dt);

 private:
  int v[5];     // year, month, day, hour, minute
  int field = 3;
  char typed[5] = "";
  float t = 0, fieldX = 0, bump = 0;
  int bumpField = -1;
  void clampDay();
  void change(int f, int d);
};
