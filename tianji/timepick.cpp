#include "timepick.h"
#include "sfx.h"
#include "system.h"

static const int kLo[5] = {2024, 1, 1, 0, 0}, kHi[5] = {2099, 12, 31, 23, 59};
static const int kWidth[5] = {4, 2, 2, 2, 2};
// x position (centre) and digit pitch of each field in "2026-10-24  15:30"
static const int kX[5] = {44, 96, 134, 175, 209}, kCell = 13;

static int daysIn(int y, int m) {
  static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
  return m == 2 && leap ? 29 : d[m - 1];
}

void TimePicker::begin() {
  struct tm tm;
  if (timeLocal(tm)) {
    v[0] = tm.tm_year + 1900, v[1] = tm.tm_mon + 1, v[2] = tm.tm_mday, v[3] = tm.tm_hour, v[4] = tm.tm_min;
  } else {
    // no clock at all: start from the day this firmware was built
    static const char* mon = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char m[4] = {__DATE__[0], __DATE__[1], __DATE__[2], 0};
    v[1] = (strstr(mon, m) - mon) / 3 + 1;
    v[2] = atoi(__DATE__ + 4);
    v[0] = atoi(__DATE__ + 7);
    v[3] = 12, v[4] = 0;
  }
  for (int i = 0; i < 5; ++i) v[i] = std::max(kLo[i], std::min(kHi[i], v[i]));
  field = timeState() == TIME_NONE ? 0 : 3;  // a restored clock is usually right except for the hour
  fieldX = field;
  typed[0] = 0;
  t = 0;
  open = true;
}

void TimePicker::clampDay() { v[2] = std::min(v[2], daysIn(v[0], v[1])); }

void TimePicker::change(int f, int d) {
  int span = kHi[f] - kLo[f] + 1;
  int hi = f == 2 ? daysIn(v[0], v[1]) : kHi[f];
  if (f == 2) span = hi - kLo[f] + 1;
  v[f] = kLo[f] + ((v[f] - kLo[f] + d) % span + span) % span;
  clampDay();
  bump = 1;
  bumpField = f;
}

bool TimePicker::key(const KeyEvent& e) {
  if (!open) return false;
  if (e.key == K_ESC || e.key == K_BACK) {
    if (e.key == K_BACK && typed[0]) {
      typed[strlen(typed) - 1] = 0;
      return true;
    }
    open = false;
    return true;
  }
  if (e.key == K_LEFT) field = (field + 4) % 5, typed[0] = 0, sfxTick(1.1f, 0.3f);
  else if (e.key == K_RIGHT || e.key == K_TAB) field = (field + 1) % 5, typed[0] = 0, sfxTick(1.1f, 0.3f);
  else if (e.key == K_UP) change(field, e.shift ? 10 : 1), typed[0] = 0, sfxTick(1.3f, 0.25f);
  else if (e.key == K_DOWN) change(field, e.shift ? -10 : -1), typed[0] = 0, sfxTick(0.9f, 0.25f);
  else if (e.key >= '0' && e.key <= '9') {
    size_t l = strlen(typed);
    typed[l] = e.key;
    typed[l + 1] = 0;
    if ((int)l + 1 >= kWidth[field]) {
      v[field] = std::max(kLo[field], std::min(field == 2 ? daysIn(v[0], v[1]) : kHi[field], atoi(typed)));
      clampDay();
      typed[0] = 0;
      bump = 1;
      bumpField = field;
      field = std::min(4, field + 1);
    }
    sfxTick(1.2f, 0.25f);
  } else if (e.key == K_ENTER || e.key == ' ') {
    timeSetLocal(v[0], v[1], v[2], v[3], v[4], 0);
    open = false;
    sfxChime(1.3f, 0.5f);
    toast("时间已校准", col::JADE);
  } else if (e.key == 'w' && S.ssid[0]) {
    ntpStart();
    open = false;
    toast("联网校时中…", col::GOLD);
  }
  return true;  // the picker is modal
}

void TimePicker::draw(float dt) {
  if (!open) return;
  t += dt;
  fieldX = approach(fieldX, field, 22, dt);
  bump = std::max(0.0f, bump - dt * 6);
  float in = easeOutCubic(clamp01(t / 0.25f));
  darkenRect(0, 0, SW, SH, 1.0f - 0.7f * in);
  int py = 30 + (int)((1 - in) * 12);
  roundPanel(10, py, SW - 20, 78, 10, col::INK2, blend565(col::INK2, col::GOLD2, in));

  // selection glow slides between fields
  float fx = lerpf(kX[(int)floorf(fieldX)], kX[std::min(4, (int)floorf(fieldX) + 1)], fieldX - floorf(fieldX));
  int fw = kWidth[field] * kCell + 6;
  glowDot(fx, py + 30, 26, col::GOLD, 0.16f);
  cv.fillRoundRect((int)fx - fw / 2, py + 45, fw, 2, 1, col::GOLD);
  // chevrons on the active field
  cv.fillTriangle((int)fx - 4, py + 12, (int)fx + 4, py + 12, (int)fx, py + 8, col::GOLD2);
  cv.fillTriangle((int)fx - 4, py + 50, (int)fx + 4, py + 50, (int)fx, py + 54, col::GOLD2);

  char b[8];
  for (int i = 0; i < 5; ++i) {
    bool on = i == field;
    if (on && typed[0]) snprintf(b, sizeof b, "%s%s", typed, ((millis() / 400) & 1) ? "_" : " ");
    else snprintf(b, sizeof b, kWidth[i] == 4 ? "%04d" : "%02d", v[i]);
    int dy = (i == bumpField) ? (int)(-bump * 3) : 0;
    textMono(b, kX[i], py + 18 + dy, &F24, on ? col::WHITE : col::DIM, kCell, AC);
  }
  // separators drawn small so the numbers carry the weight
  cv.fillRect(73, py + 37, 5, 2, col::MUTE);
  cv.fillRect(112, py + 37, 5, 2, col::MUTE);
  cv.fillRect(191, py + 32, 2, 2, col::MUTE);
  cv.fillRect(191, py + 41, 2, 2, col::MUTE);
  text(S.ssid[0] ? "↑↓ 调整   Enter 确认   W 联网" : "↑↓ 调整   ←→ 切换   Enter 确认", SW / 2, py + 60, &F12, col::MUTE, AC);
}
