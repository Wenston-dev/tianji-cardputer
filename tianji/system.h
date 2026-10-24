// Persistent settings, wall clock, battery and backlight/power management.
#pragma once
#include <Arduino.h>
#include <time.h>

struct Settings {
  int16_t birthY = 0;
  int8_t birthM = 0, birthD = 0;
  uint8_t lifespan = 80;
  int16_t tzMin = 480;  // UTC+8
  char ssid[33] = "";
  char pass[65] = "";
  uint8_t brightness = 150;
  uint8_t volume = 110;
  uint8_t rateIdx = 0;   // kRates
  uint8_t gainIdx = 1;   // kGains
  bool recDim = true;
  uint8_t recOffIdx = 1;  // kRecOff
  bool sfx = true;
  bool usbAuto = true;  // plugging into a computer offers the USB drive
  uint8_t ntpMode = 1;  // 0 off, 1 at boot, 2 at boot + daily
  bool wifiAutoOff = true;  // radio off as soon as the clock is synced
  uint8_t pomoOffIdx = 1;   // kPomoOff
  bool ble = true;          // Bluetooth for the Kindle page turner
  bool pomoAuto = false;    // after a break, start the next focus block without waiting for Space
};
extern Settings S;

static constexpr uint32_t kRates[] = {16000, 24000, 32000, 44100};
static constexpr uint8_t kGains[] = {6, 8, 10, 10};  // ES8311 PGA in 3 dB steps (18..30 dB, +12 dB digital on the last)
static constexpr uint16_t kRecOff[] = {15, 30, 60, 0};  // seconds until the screen goes dark, 0 = never
static constexpr uint16_t kPomoOff[] = {0, 30, 60, 180};  // focus screen-off delay, 0 = never

void settingsLoad();
void settingsSave();
bool birthdaySet();

enum TimeState { TIME_NONE = 0, TIME_RESTORED = 1, TIME_SET = 2, TIME_NTP = 3 };
void timeInit();
void timeTick();
int timeState();
// The clock can be trusted (set by hand or synced), as opposed to restored from the last save.
bool timeTrusted();
void timePersistNow();  // save the clock to flash now (before standby)
bool timeLocal(struct tm& out);
double nowSec();  // unix seconds with sub-second precision
void timeSetLocal(int y, int mo, int d, int h, int mi, int s);
void timeApplyTz();
// days since 1970-01-01 for a civil date (proleptic Gregorian)
long daysFromCivil(int y, unsigned m, unsigned d);

enum NtpState { NTP_IDLE, NTP_BUSY, NTP_OK, NTP_FAIL };
void ntpStart();
int ntpState();
void ntpAbort();  // give the radio memory back now (e.g. for Bluetooth)
bool wifiOn();

int batteryPct();
bool batteryCharging();

// Backlight: call powerActivity() on every key press. Returns true when the screen was dark and the
// key should only wake it.
bool powerActivity();
void powerTick(float dt);
bool screenAwake();
float idleSeconds();
void setBacklightNow();
// A screen can ask for the display to go dark after `sec` idle seconds (0 = off).
void powerAppDark(uint16_t sec);
