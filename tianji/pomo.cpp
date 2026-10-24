#include "pomo.h"
#include <Preferences.h>
#include <SD.h>
#include "rec.h"
#include "system.h"

namespace pomo {

static Preset presets[kMax];
static int n = 0;

static Phase ph = IDLE;
static int cur = 0, rounds = 0;
static uint32_t phaseMs = 0, startMs = 0, pausedAt = 0, pausedTotal = 0;
static bool isPaused = false;
static time_t focusStartEpoch = 0;
static int todayMin = -1;  // cache, -1 = unknown

static void defaults() {
  const Preset d[] = {{"工作", 25, 5, 15, 4}, {"阅读", 45, 10, 20, 3}, {"学习", 50, 10, 20, 2}, {"Deep Work", 90, 15, 30, 2}};
  n = sizeof d / sizeof d[0];
  memcpy(presets, d, sizeof d);
}

static void save() {
  Preferences p;
  p.begin("pomo", false);
  p.putUChar("n", n);
  p.putBytes("p", presets, sizeof(Preset) * n);
  p.end();
}

void begin() {
  Preferences p;
  p.begin("pomo", true);
  int k = p.getUChar("n", 0);
  if (k > 0 && k <= kMax && p.getBytesLength("p") == sizeof(Preset) * k) {
    n = k;
    p.getBytes("p", presets, sizeof(Preset) * n);
  } else {
    defaults();
  }
  p.end();
}

int count() { return n; }
const Preset& get(int i) { return presets[std::max(0, std::min(i, n - 1))]; }

void set(int i, const Preset& pr) {
  if (i < 0 || i > n || i >= kMax) return;
  presets[i] = pr;
  presets[i].name[sizeof(pr.name) - 1] = 0;
  if (i == n) ++n;
  save();
}

void remove(int i) {
  if (i < 0 || i >= n || n <= 1) return;
  memmove(presets + i, presets + i + 1, sizeof(Preset) * (n - i - 1));
  --n;
  if (cur >= n) cur = n - 1;
  save();
}

static uint32_t lenOf(Phase p) {
  const Preset& pr = presets[cur];
  uint32_t m = p == FOCUS ? pr.focus : p == SHORT ? pr.shortBreak : pr.longBreak;
  return std::max<uint32_t>(1, m) * 60000UL;
}

static void enter(Phase p, bool wait) {
  ph = p;
  phaseMs = lenOf(p);
  startMs = millis();
  pausedTotal = 0;
  isPaused = wait;
  pausedAt = startMs;
  if (p == FOCUS) focusStartEpoch = time(nullptr);
}

// One line per focus block: start (unix), seconds focused, completed flag, preset name.
static void logFocus(uint32_t focusedMs, bool completed) {
  if (focusedMs < 60000) return;  // ignore false starts
  todayMin = -1;
  if (!rec::sdBegin()) return;
  rec::sdLock();
  if (!SD.exists("/POMO")) SD.mkdir("/POMO");
  File f = SD.open("/POMO/focus.csv", FILE_APPEND);
  if (f) {
    f.printf("%ld,%lu,%d,%s\n", (long)focusStartEpoch, (unsigned long)(focusedMs / 1000), completed ? 1 : 0, presets[cur].name);
    f.close();
  }
  rec::sdUnlock();
}

static uint32_t elapsed() {
  uint32_t now = isPaused ? pausedAt : millis();
  return now - startMs - pausedTotal;
}

void start(int i) {
  cur = std::max(0, std::min(i, n - 1));
  rounds = 0;
  enter(FOCUS, false);
}

void stop() {
  if (ph == FOCUS) logFocus(elapsed(), false);
  ph = IDLE;
  isPaused = false;
}

void togglePause() {
  if (ph == IDLE) return;
  if (isPaused) {
    pausedTotal += millis() - pausedAt;
    isPaused = false;
  } else {
    pausedAt = millis();
    isPaused = true;
  }
}

bool paused() { return isPaused; }

static Phase nextAfterFocus() {
  ++rounds;
  return (presets[cur].every && rounds % presets[cur].every == 0) ? LONG : SHORT;
}

void skip() {
  if (ph == FOCUS) {
    logFocus(elapsed(), false);
    enter(nextAfterFocus(), false);
  } else if (ph != IDLE) {
    enter(FOCUS, false);
  }
}

Phase phase() { return ph; }
int presetIdx() { return cur; }
int round() { return rounds; }
uint32_t phaseTotalMs() { return phaseMs; }
uint32_t remainingMs() {
  if (ph == IDLE) return 0;
  uint32_t e = elapsed();
  return e >= phaseMs ? 0 : phaseMs - e;
}
float progress() { return ph == IDLE ? 0 : std::min(1.0f, (float)elapsed() / phaseMs); }

bool tick(Phase* ended) {
  if (ph == IDLE || isPaused || elapsed() < phaseMs) return false;
  Phase was = ph;
  if (ph == FOCUS) {
    logFocus(phaseMs, true);
    enter(nextAfterFocus(), false);  // breaks start on their own
  } else {
    enter(FOCUS, true);  // the next focus block waits for the user
  }
  if (ended) *ended = was;
  return true;
}

void stats(Day days[7], int& streak) {
  memset(days, 0, sizeof(Day) * 7);
  streak = 0;
  struct tm t;
  if (!timeLocal(t)) return;
  long today = daysFromCivil(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  static bool active[60];
  memset(active, 0, sizeof active);
  if (!rec::sdBegin()) return;
  rec::sdLock();
  File f = SD.open("/POMO/focus.csv");
  if (f) {
    char line[96];
    while (f.available()) {
      int len = f.readBytesUntil('\n', line, sizeof line - 1);
      line[len] = 0;
      long start;
      unsigned long secs;
      if (sscanf(line, "%ld,%lu", &start, &secs) != 2) continue;
      long day = (long)floor((start + S.tzMin * 60.0) / 86400.0);
      long ago = today - day;
      if (ago >= 0 && ago < 7) {
        days[6 - ago].minutes += secs / 60;
        days[6 - ago].sessions++;
      }
      if (ago >= 0 && ago < 60) active[ago] = true;
    }
    f.close();
  }
  rec::sdUnlock();
  // today may still be empty without breaking a streak that ended yesterday
  int k = active[0] ? 0 : 1;
  while (k < 60 && active[k]) ++streak, ++k;
  todayMin = days[6].minutes;
}

int todayMinutes() {
  if (todayMin < 0) {
    Day d[7];
    int s;
    stats(d, s);
  }
  return std::max(0, todayMin);
}

}  // namespace pomo
