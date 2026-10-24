#include "system.h"
#include <M5Cardputer.h>
#include <Preferences.h>
#include <WiFi.h>
#include <sys/time.h>
#include "esp_sntp.h"
#include "rec.h"
#include "pomo.h"
#include "kclick.h"
#include "standby.h"

Settings S;
static Preferences prefs;

void settingsLoad() {
  prefs.begin("tianji", true);
  S.birthY = prefs.getShort("by", 0);
  S.birthM = prefs.getChar("bm", 0);
  S.birthD = prefs.getChar("bd", 0);
  S.lifespan = prefs.getUChar("life", 80);
  S.tzMin = prefs.getShort("tz", 480);
  prefs.getString("ssid", S.ssid, sizeof S.ssid);
  prefs.getString("pass", S.pass, sizeof S.pass);
  S.brightness = prefs.getUChar("bri", 150);
  S.volume = prefs.getUChar("vol", 110);
  S.rateIdx = prefs.getUChar("rate", 0) % 4;
  S.gainIdx = prefs.getUChar("gain", 1) % 4;
  S.recDim = prefs.getBool("rdim", true);
  S.recOffIdx = prefs.getUChar("roff", 1) % 4;
  S.sfx = prefs.getBool("sfx", true);
  S.usbAuto = prefs.getBool("usb", true);
  S.ntpMode = prefs.getUChar("ntpm", 1) % 3;
  S.wifiAutoOff = prefs.getBool("wfoff", true);
  S.pomoOffIdx = prefs.getUChar("poff", 1) % 4;
  S.ble = prefs.getBool("ble", true);
  S.pomoAuto = prefs.getBool("pauto", false);
  prefs.end();
}

void settingsSave() {
  prefs.begin("tianji", false);
  prefs.putShort("by", S.birthY);
  prefs.putChar("bm", S.birthM);
  prefs.putChar("bd", S.birthD);
  prefs.putUChar("life", S.lifespan);
  prefs.putShort("tz", S.tzMin);
  prefs.putString("ssid", S.ssid);
  prefs.putString("pass", S.pass);
  prefs.putUChar("bri", S.brightness);
  prefs.putUChar("vol", S.volume);
  prefs.putUChar("rate", S.rateIdx);
  prefs.putUChar("gain", S.gainIdx);
  prefs.putBool("rdim", S.recDim);
  prefs.putUChar("roff", S.recOffIdx);
  prefs.putBool("sfx", S.sfx);
  prefs.putBool("usb", S.usbAuto);
  prefs.putUChar("ntpm", S.ntpMode);
  prefs.putBool("wfoff", S.wifiAutoOff);
  prefs.putUChar("poff", S.pomoOffIdx);
  prefs.putBool("ble", S.ble);
  prefs.putBool("pauto", S.pomoAuto);
  prefs.end();
}

bool birthdaySet() { return S.birthY > 1900 && S.birthM >= 1 && S.birthD >= 1; }

// ---------------------------------------------------------------- time

static int tState = TIME_NONE;
// Survives esp_restart() and deep sleep (not power loss): whether the running clock is trusted.
RTC_NOINIT_ATTR static uint32_t rtcTimeMagic, rtcTimeState;
static constexpr uint32_t TIME_MAGIC = 0x7469346d;

static void setTimeState(int s) {
  tState = s;
  rtcTimeState = s;
  rtcTimeMagic = TIME_MAGIC;
}
static uint32_t lastPersist = 0;

long daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (long)doe - 719468;
}

void timeApplyTz() {
  char tz[24];
  int m = S.tzMin;
  // POSIX TZ offsets have the opposite sign
  snprintf(tz, sizeof tz, "UTC%c%d:%02d", m >= 0 ? '-' : '+', abs(m) / 60, abs(m) % 60);
  setenv("TZ", tz, 1);
  tzset();
}

static void persistTime() {
  prefs.begin("tianji", false);
  prefs.putULong64("epoch", (uint64_t)time(nullptr));
  prefs.end();
  lastPersist = millis();
}

void timeInit() {
  timeApplyTz();
  if (time(nullptr) > 1600000000) {
    // the RTC kept counting: a restart or a wake from standby; keep what we knew about the clock
    setTimeState(rtcTimeMagic == TIME_MAGIC && rtcTimeState <= TIME_NTP ? (int)rtcTimeState : TIME_SET);
    lastPersist = millis();
    return;
  }
  prefs.begin("tianji", true);
  uint64_t e = prefs.getULong64("epoch", 0);
  prefs.end();
  if (e > 1600000000) {
    struct timeval tv = {(time_t)e, 0};
    settimeofday(&tv, nullptr);
    setTimeState(TIME_RESTORED);
  }
  lastPersist = millis();
}

int timeState() { return tState; }
bool timeTrusted() { return tState >= TIME_SET; }
void timePersistNow() {
  if (tState != TIME_NONE) persistTime();
}

bool timeLocal(struct tm& out) {
  time_t t = time(nullptr);
  if (t < 1600000000) return false;
  localtime_r(&t, &out);
  return true;
}

double nowSec() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return tv.tv_sec + tv.tv_usec * 1e-6;
}

void timeSetLocal(int y, int mo, int d, int h, int mi, int s) {
  long days = daysFromCivil(y, mo, d);
  time_t t = (time_t)days * 86400 + h * 3600 + mi * 60 + s - S.tzMin * 60;
  struct timeval tv = {t, 0};
  settimeofday(&tv, nullptr);
  setTimeState(TIME_SET);
  persistTime();
}

static volatile bool ntpSynced = false;
static int nState = NTP_IDLE;
static uint32_t ntpT0 = 0, lastSyncMs = 0;
static bool resumeBle = false;  // Bluetooth paused for a sync comes back afterwards
static bool synced = false;

static void onSync(struct timeval*) { ntpSynced = true; }

void ntpStart() {
  if (!S.ssid[0]) {
    nState = NTP_FAIL;
    return;
  }
  ntpSynced = false;
  if (kclick::on()) {
    kclick::end();
    resumeBle = true;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(S.ssid, S.pass);
  sntp_set_time_sync_notification_cb(onSync);
  char tz[24];
  snprintf(tz, sizeof tz, "UTC%c%d:%02d", S.tzMin >= 0 ? '-' : '+', abs(S.tzMin) / 60, abs(S.tzMin) % 60);
  configTzTime(tz, "ntp.aliyun.com", "cn.pool.ntp.org", "pool.ntp.org");
  nState = NTP_BUSY;
  ntpT0 = millis();
}

int ntpState() { return nState; }

void ntpAbort() {
  if (nState == NTP_BUSY) nState = NTP_FAIL;
  resumeBle = false;
  if (wifiOn()) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
}
bool wifiOn() { return WiFi.getMode() != WIFI_OFF; }

void timeTick() {
  if (nState == NTP_BUSY) {
    if (ntpSynced) {
      nState = NTP_OK;
      setTimeState(TIME_NTP);
      standbyOnSync();
      synced = true;
      lastSyncMs = millis();
      persistTime();
    } else if (millis() - ntpT0 > 25000) {
      nState = NTP_FAIL;
    }
    if (nState != NTP_BUSY && (S.wifiAutoOff || nState == NTP_FAIL)) {
      // radio off again; it is only needed for the sync
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
    }
    if (nState != NTP_BUSY && resumeBle && !wifiOn()) {
      resumeBle = false;
      kclick::begin();
    }
  }
  // daily re-sync keeps the crystal drift in check
  if (S.ntpMode == 2 && S.ssid[0] && nState != NTP_BUSY && synced && millis() - lastSyncMs > 24UL * 3600 * 1000) {
    lastSyncMs = millis();
    ntpStart();
  }
  if (tState != TIME_NONE && millis() - lastPersist > 60000) persistTime();
}

// ---------------------------------------------------------------- battery

static int batPct = 100;
static bool batChg = false;
static uint32_t batT = 0;

static void batPoll() {
  if (batT && millis() - batT < 5000) return;
  batT = millis();
  int p = M5.Power.getBatteryLevel();
  if (p >= 0) batPct = std::min(100, p);
  batChg = M5.Power.isCharging() == m5::Power_Class::is_charging_t::is_charging;
}
int batteryPct() {
  batPoll();
  return batPct;
}
bool batteryCharging() {
  batPoll();
  return batChg;
}

// ---------------------------------------------------------------- backlight / power

static uint32_t lastAct = 0;
static float bri = 0;      // current backlight level
static float briTarget = 0;
static bool cpuLow = false;
static uint16_t appDark = 0;

void powerAppDark(uint16_t sec) { appDark = sec; }

float idleSeconds() { return (millis() - lastAct) / 1000.0f; }

bool powerActivity() {
  bool wasOff = briTarget == 0 && bri < 3;
  lastAct = millis();
  return wasOff;
}

bool screenAwake() { return briTarget > 0 || bri > 1; }

void setBacklightNow() {
  briTarget = S.brightness;
  bri = briTarget;
  M5.Display.setBrightness((uint8_t)bri);
}

void powerTick(float dt) {
  float idle = idleSeconds();
  float target = S.brightness;
  bool recording = rec::state() != rec::IDLE;
  bool focusing = pomo::phase() == pomo::FOCUS && !pomo::paused();
  if (appDark && !recording) {
    if (idle > 5) target = std::max(8.0f, S.brightness * 0.1f);
    if (idle > appDark) target = 0;
  } else if (focusing && kPomoOff[S.pomoOffIdx] && !recording) {
    if (idle > 5) target = std::max(8.0f, S.brightness * 0.1f);
    if (idle > kPomoOff[S.pomoOffIdx]) target = 0;
  } else if (recording && S.recDim) {
    uint16_t off = rec::presetOffSec();
    if (off == 0xFFFF) off = kRecOff[S.recOffIdx];
    if (idle > 3) target = std::max(6.0f, S.brightness * 0.06f);
    if (off && idle > off) target = 0;
  } else if (idle > 120) {
    target = std::max(20.0f, S.brightness * 0.35f);
  }
  briTarget = target;
  float rate = target < bri ? 2.2f : 14.0f;  // fall slowly, rise fast
  bri = target + (bri - target) * expf(-rate * dt);
  if (fabsf(bri - target) < 0.5f) bri = target;
  M5.Display.setBrightness((uint8_t)bri);

  // While recording in the dark, drop the CPU clock. APB stays at 80 MHz so I2S/SPI are untouched.
  bool wantLow = (recording || focusing) && target == 0 && bri < 1;
  if (wantLow != cpuLow) {
    cpuLow = wantLow;
    setCpuFrequencyMhz(cpuLow ? 80 : 240);
  }
}
