#include "standby.h"
#include <M5Cardputer.h>
#include <SD.h>
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include <Preferences.h>
#include <sys/time.h>
#include "gfx.h"
#include "kclick.h"
#include "rec.h"
#include "system.h"

static constexpr gpio_num_t PIN_G0 = GPIO_NUM_0;
static constexpr gpio_num_t PIN_KB_INT = GPIO_NUM_11;  // TCA8418 INT, active low on a key event
static constexpr gpio_num_t PIN_BL = GPIO_NUM_38;      // display backlight
static constexpr uint8_t TCA8418 = 0x34, ES8311 = 0x18;

bool wokeFromStandby() { return esp_reset_reason() == ESP_RST_DEEPSLEEP; }

// ---------------------------------------------------------------- clock rate correction
// Measured on a Cardputer ADV: 301.0 s counted for 297.6 s asleep -> the sleep clock runs ~1.15 % fast.
static constexpr int32_t DEFAULT_PPM = 11450;
RTC_NOINIT_ATTR static double rtcSleepAt;  // trusted wall clock right before sleeping
RTC_NOINIT_ATTR static uint32_t rtcSleepMagic;
static constexpr uint32_t SLEEP_MAGIC = 0x5133ee70;
static double wakeAt = 0, wakeRtcElapsed = 0, wakeCorrElapsed = 0;
static int64_t wakeUs = 0;
static int32_t ppm = DEFAULT_PPM;

static int32_t loadPpm() {
  Preferences p;
  int32_t v = DEFAULT_PPM;
  if (p.begin("tianji", true)) {
    v = p.getInt("rtcppm", DEFAULT_PPM);
    p.end();
  }
  return v;
}

void standbyCorrectClock() {
  ppm = loadPpm();
  bool ok = wokeFromStandby() && rtcSleepMagic == SLEEP_MAGIC;
  rtcSleepMagic = 0;
  if (!ok) return;
  double now = nowSec();
  double el = now - rtcSleepAt;
  if (el <= 0 || el > 400.0 * 86400) return;
  double corr = el * 1e6 / (1e6 + ppm);
  double t = rtcSleepAt + corr;
  struct timeval tv = {(time_t)t, (suseconds_t)((t - floor(t)) * 1e6)};
  settimeofday(&tv, nullptr);
  wakeAt = t;
  wakeUs = esp_timer_get_time();
  wakeRtcElapsed = el;
  wakeCorrElapsed = corr;
}

void standbyOnSync() {
  if (wakeRtcElapsed < 600) return;  // too short to learn from
  // where the corrected clock would be now, versus what NTP just said
  double expect = wakeAt + (esp_timer_get_time() - wakeUs) * 1e-6;
  double off = nowSec() - expect;
  double trueEl = wakeCorrElapsed + off;
  if (trueEl <= 0) return;
  double p = (wakeRtcElapsed / trueEl - 1) * 1e6;
  if (fabs(p) > 50000) return;
  ppm = (int32_t)lround((ppm + p) / 2);  // move halfway: temperature makes single readings noisy
  Preferences pr;
  if (pr.begin("tianji", false)) {
    pr.putInt("rtcppm", ppm);
    pr.end();
  }
  wakeRtcElapsed = 0;
}

void standbyBootFixup() {
  gpio_hold_dis(PIN_BL);
  gpio_deep_sleep_hold_dis();
  if (wokeFromStandby()) {
    // the wake pins were handed to the RTC domain; give them back to the digital matrix
    rtc_gpio_deinit(PIN_G0);
    rtc_gpio_deinit(PIN_KB_INT);
  }
}

// Read out every pending key event so the INT line goes back high.
static void flushKeyboard() {
  for (int i = 0; i < 20; ++i) {
    uint8_t n = M5.In_I2C.readRegister8(TCA8418, 0x03, 400000) & 0x0F;  // KEY_LCK_EC: events queued
    if (!n) break;
    while (n--) M5.In_I2C.readRegister8(TCA8418, 0x04, 400000);       // KEY_EVENT_A
  }
  M5.In_I2C.writeRegister8(TCA8418, 0x02, 0x03, 400000);  // clear INT_STAT
}

static bool anyKeyHeld() {
  M5Cardputer.update();
  return !M5Cardputer.Keyboard.keyList().empty() || digitalRead(PIN_G0) == LOW;
}

static void goodbye() {
  // the clock, then the backlight fades out
  struct tm tm;
  char b[8] = "";
  if (timeLocal(tm)) snprintf(b, sizeof b, "%02d:%02d", tm.tm_hour, tm.tm_min);
  for (int i = 0; i <= 20; ++i) {
    float k = i / 20.0f;
    cv.fillSprite(col::INK);
    if (b[0]) textMono(b, SW / 2, 40, &FBIG, blend565(col::WHITE, col::INK, k), 19, AC);
    text("待机", SW / 2, 86, &F12, blend565(col::DIM, col::INK, k), AC);
    glowDot(SW / 2, 100, 10 + 6 * (1 - k), col::GOLD, 0.3f * (1 - k));
    present();
    M5.Display.setBrightness((uint8_t)(S.brightness * (1 - easeInOutCubic(k))));
    delay(25);
  }
}

void standbyEnter(uint32_t timerSec) {
  // a recording is finished properly so its file is complete
  if (rec::state() != rec::IDLE) {
    rec::stop();
    uint32_t t0 = millis();
    while (rec::state() != rec::IDLE && millis() - t0 < 5000) {
      rec::service();
      delay(10);
    }
  }
  timePersistNow();
  settingsSave();
  goodbye();

  kclick::end();
  ntpAbort();
  M5.Speaker.end();
  // codec analog off
  M5.In_I2C.writeRegister8(ES8311, 0x0D, 0xFC, 400000);
  M5.In_I2C.writeRegister8(ES8311, 0x0E, 0x6A, 400000);
  M5.In_I2C.writeRegister8(ES8311, 0x00, 0x00, 400000);
  M5.Imu.sleep();
  SD.end();
  M5.Display.setBrightness(0);
  M5.Display.sleep();
  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, LOW);
  gpio_hold_en(PIN_BL);
  gpio_deep_sleep_hold_en();

  // wait for the key that asked for standby to be let go, then empty the keyboard's queue
  uint32_t t0 = millis();
  while (anyKeyHeld() && millis() - t0 < 10000) delay(20);
  delay(60);
  flushKeyboard();
  delay(20);

  uint64_t mask = 1ULL << PIN_G0;
  if (digitalRead(PIN_KB_INT) == HIGH) mask |= 1ULL << PIN_KB_INT;  // else only G0 can wake us
  rtc_gpio_pullup_en(PIN_G0);
  rtc_gpio_pulldown_dis(PIN_G0);
  rtc_gpio_pullup_en(PIN_KB_INT);
  rtc_gpio_pulldown_dis(PIN_KB_INT);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);  // keep those pull-ups alive
  esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);
  if (timerSec) esp_sleep_enable_timer_wakeup((uint64_t)timerSec * 1000000ULL);
  if (timeTrusted()) {
    rtcSleepAt = nowSec();
    rtcSleepMagic = SLEEP_MAGIC;
  }
  esp_deep_sleep_start();
}
