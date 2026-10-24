// Pomodoro engine: presets (kept in NVS), a background timer that survives leaving the screen,
// and a focus log on the SD card (/POMO/focus.csv) for the statistics view.
#pragma once
#include <Arduino.h>

namespace pomo {

struct Preset {
  char name[20];
  uint8_t focus, shortBreak, longBreak, every;  // minutes, minutes, minutes, focus rounds per long break
};

enum Phase { IDLE, FOCUS, SHORT, LONG };

void begin();
int count();
const Preset& get(int i);
void set(int i, const Preset& p);  // i == count() appends
void remove(int i);
static constexpr int kMax = 12;

void start(int preset);
void stop();
void togglePause();
bool paused();
void skip();

Phase phase();
int presetIdx();
int round();             // focus rounds finished in this run
uint32_t phaseTotalMs();
uint32_t remainingMs();
float progress();        // 0..1 through the current phase

// Call from loop(). Returns true once when a phase ends (`ended` = the phase that just finished).
bool tick(Phase* ended);

struct Day {
  int minutes, sessions;
};
// Last 7 days (index 6 = today), plus the run of consecutive days with any focus.
void stats(Day days[7], int& streak);
int todayMinutes();

}  // namespace pomo
