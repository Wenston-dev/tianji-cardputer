// Fake hardware/services for the host preview build.
#include "Arduino.h"
#include "M5Cardputer.h"
#include "system.h"
#include "input.h"
#include "sfx.h"
#include "rec.h"
#include "app.h"
#include <deque>
#include <sys/time.h>

uint64_t g_us = 0;
FakeM5 M5;
Settings S;
double g_epoch0 = 0;  // fake wall clock at g_us == 0

// --- system.h
void settingsLoad() {}
void settingsSave() {}
bool birthdaySet() { return S.birthY > 1900; }
int g_tstate = TIME_NTP;
void timeInit() {}
void timeTick() {}
int timeState() { return g_tstate; }
double nowSec() { return g_epoch0 + g_us * 1e-6; }
bool timeLocal(struct tm& out) {
  time_t t = (time_t)(nowSec() + S.tzMin * 60);
  gmtime_r(&t, &out);
  return true;
}
void timeSetLocal(int, int, int, int, int, int) { g_tstate = TIME_SET; }
void timeApplyTz() {}
long daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (long)doe - 719468;
}
void ntpStart() {}
int ntpState() { return NTP_IDLE; }
int batteryPct() { return 76; }
bool batteryCharging() { return false; }
bool powerActivity() { return false; }
void powerTick(float) {}
bool screenAwake() { return true; }
float idleSeconds() { return 0; }
void setBacklightNow() {}

// --- input.h
std::deque<KeyEvent> g_keys;
std::vector<uint16_t> g_held;
void inputInit() {}
void inputPoll() {}
bool inputNext(KeyEvent& e) { if (g_keys.empty()) return false; e = g_keys.front(); g_keys.pop_front(); return true; }
bool keyDown(uint16_t k) { return std::find(g_held.begin(), g_held.end(), k) != g_held.end(); }
void inputTextMode(bool) {}
void inputFlush() { g_keys.clear(); }
float g_shake = 0;
float shakeLevel() { return g_shake; }
bool imuReady() { return true; }

// --- sfx.h
void sfxInit() {}
void sfxTick(float, float) {}
void sfxKnock(float, float) {}
void sfxClack(float, float) {}
void sfxRing(float, float) {}
void sfxChime(float, float) {}
void sfxWhoosh(float) {}
void sfxBeep(float, uint32_t) {}
bool sfxAvailable() { return false; }

// --- rec.h (fake recorder state for UI previews)
namespace rec {
const Preset kPresets[3] = {{"随手录", "标准录音", -1, 0xFFFF}, {"长时录音", "16kHz 8秒息屏", 0, 8}, {"高保真", "44.1kHz 音乐", 3, 0xFFFF}};
State g_state = IDLE;
uint32_t g_elapsed = 0;
bool sdBegin() { return true; }
bool sdOk() { return true; }
uint64_t sdFreeBytes() { return 28ull * 1024 * 1024 * 1024; }
void sdLock() {}
void sdUnlock() {}
static fs::FS g_fs;
fs::FS& fs() { return g_fs; }
bool start(int) { g_state = RECORDING; return true; }
void stop() { g_state = IDLE; }
void togglePause() { g_state = g_state == RECORDING ? PAUSED : RECORDING; }
void mark() {}
void service() {}
State state() { return g_state; }
uint32_t elapsedMs() { return g_elapsed + millis(); }
uint64_t dataBytes() { return (uint64_t)elapsedMs() * 32; }
uint32_t sampleRate() { return 16000; }
const char* path() { return "/REC/20261023_153012.wav"; }
int presetIdx() { return 0; }
uint16_t presetOffSec() { return 0xFFFF; }
uint32_t overruns() { return 0; }
int markCount() { return 2; }
const char* lastError() { return ""; }
bool takeFinished(char*, size_t, uint32_t*) { return false; }
int history(uint8_t* out, int n) {
  float t = millis() / 1000.0f;
  for (int i = 0; i < n; ++i) {
    float k = (t * 32 - (n - i));
    float env = 0.5f + 0.5f * sinf(k * 0.11f) * sinf(k * 0.037f + 1);
    float v = env * (0.6f + 0.4f * sinf(k * 1.7f)) * 230 + (rand() % 20);
    out[i] = (uint8_t)std::max(0.0f, std::min(255.0f, v));
  }
  return n;
}
float peakLevel() { return 0.72f; }
int scope(int16_t* out, int n) {
  for (int i = 0; i < n; ++i) out[i] = (int16_t)(9000 * sinf(i * 0.31f + millis() * 0.01f) * sinf(i * 0.05f));
  return n;
}
bool readWavInfo(File&, WavInfo&) { return false; }
}  // namespace rec

// --- newer system/rec APIs
bool wifiOn() { return false; }
namespace rec {
uint32_t historyTotal() { return millis() * 32 / 1000; }
uint8_t historyAt(uint32_t idx) {
  float k = idx;
  float env = 0.5f + 0.5f * sinf(k * 0.11f) * sinf(k * 0.037f + 1);
  return (uint8_t)std::max(0.0f, std::min(255.0f, env * (0.6f + 0.4f * sinf(k * 1.7f)) * 230 + (idx * 7919 % 20)));
}
float historyRate() { return 32; }
}  // namespace rec

// --- fake pomodoro engine
#include "pomo.h"
namespace pomo {
static Preset ps[kMax] = {{"工作", 25, 5, 15, 4}, {"阅读", 45, 10, 20, 3}, {"学习", 50, 10, 20, 2}, {"Deep Work", 90, 15, 30, 2}};
static int np = 4, curp = 0, rnd = 0;
static Phase phs = IDLE;
static bool pz = false;
static uint32_t st = 0;
void begin() {}
int count() { return np; }
const Preset& get(int i) { return ps[i]; }
void set(int i, const Preset& p) { ps[i] = p; if (i == np) ++np; }
void remove(int i) { memmove(ps + i, ps + i + 1, sizeof(Preset) * (np - i - 1)); --np; }
void start(int i) { curp = i; phs = FOCUS; st = millis() - 7 * 60000; rnd = 2; pz = false; }
void stop() { phs = IDLE; }
void togglePause() { pz = !pz; }
bool paused() { return pz; }
void skip() {}
Phase phase() { return phs; }
int presetIdx() { return curp; }
int round() { return rnd; }
uint32_t phaseTotalMs() { return ps[curp].focus * 60000; }
uint32_t remainingMs() { uint32_t e = millis() - st; return e > phaseTotalMs() ? 0 : phaseTotalMs() - e; }
float progress() { return phs == IDLE ? 0 : (float)(millis() - st) / phaseTotalMs(); }
bool tick(Phase*) { return false; }
void stats(Day d[7], int& streak) { int m[7] = {50, 75, 0, 100, 125, 25, 75}; for (int i = 0; i < 7; ++i) d[i] = {m[i], m[i] / 25}; streak = 3; }
int todayMinutes() { return 75; }
}  // namespace pomo

// --- page turner fakes
#include "kclick.h"
void powerAppDark(uint16_t) {}
namespace kclick {
int g_state = READY;
uint32_t g_pages = 128;
bool begin() { return true; }
void end() {}
bool on() { return g_state != OFF; }
State state() { return (State)g_state; }
bool bonded() { return true; }
void forget() {}
void wake() {}
void service() {}
bool tap(Key k) { if (k == NEXT || k == PREV) ++g_pages; return true; }
uint32_t pages() { return g_pages; }
}  // namespace kclick
void sfxRelease() {}
void ntpAbort() {}

// --- standby / time trust / G0 hold
bool timeTrusted() { return g_tstate >= TIME_SET; }
void timePersistNow() {}
void standbyEnter(uint32_t) {}
void standbyBootFixup() {}
bool wokeFromStandby() { return false; }
float g0Held() { return 0; }
