// 天机 TianJi — a life countdown, divination toys and a field recorder for the M5Stack Cardputer ADV.
#include <M5Cardputer.h>
#include "app.h"
#include "rec.h"
#include "sfx.h"
#include "system.h"
#include "usbdrive.h"
#include "pomo.h"
#include "kclick.h"
#include "standby.h"
#ifdef SELFTEST
static void note(int slot, const char* fmt, ...);
#endif


static App* apps[APP_COUNT];
static AppId cur = APP_HOME;
static uint32_t lastUs = 0;

AppId currentApp() { return cur; }

void go(AppId id) {
  if (id == cur) return;
  bool forward = id != APP_HOME;
  apps[cur]->leave();
  inputTextMode(false);
  cur = id;
  apps[cur]->enter();
  startWipe(forward);
}

void quickRecord(int preset) {
  if (rec::state() != rec::IDLE || usbdrive::active()) return;
  if (rec::start(preset)) {
    toast(rec::kPresets[preset].name, col::EMBER);
  } else {
    toast(rec::lastError(), col::CINNABAR);
  }
  go(APP_REC);
}

static bool globalKey(const KeyEvent& e) {
  if (cur == APP_USB) return false;  // the card belongs to the computer right now
  if (e.key == K_G0 || (e.ctrl && e.key == 'r')) {
    if (rec::state() == rec::IDLE) quickRecord(0);
    else rec::stop();
    return true;
  }
  if (e.fn && (e.key == '1' || e.key == '2' || e.key == '3')) {
    quickRecord(e.key - '1');
    return true;
  }
  return false;
}

static void recDot() {
  if (cur != APP_HOME && cur != APP_POMO && pomo::phase() != pomo::IDLE) drawPomoBadge(SW - 20, 7, false);
  if (rec::state() == rec::IDLE || apps[cur]->drawsStatus()) return;
  float p = 0.5f + 0.5f * sinf(millis() / 300.0f);
  glowDot(SW - 7, 6, 7, col::CINNABAR, 0.35f * p);
  cv.fillCircle(SW - 7, 6, 2, rec::state() == rec::RECORDING ? col::CINNABAR : col::DIM);
}

#ifdef SLEEPTEST
#include "esp_partition.h"
#include "esp_sleep.h"
// Build with -DSLEEPTEST: sync over NTP, sleep 300 s on a timer, wake, sync again and record how far
// the clock drifted while asleep. Notes go to the spiffs partition (esptool read_flash 0x610000).
RTC_NOINIT_ATTR static double stSleepAt;
RTC_NOINIT_ATTR static uint32_t stMagic;
static double stWakeDev = 0;
static uint32_t stWakeUs = 0;
static void stNote(int slot, const char* fmt, ...) {
  const esp_partition_t* p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "spiffs");
  if (!p) return;
  char line[200] = {0};
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line, sizeof line - 1, fmt, ap);
  va_end(ap);
  strcat(line, "\n");
  esp_partition_write(p, slot * 200, line, sizeof line);
}
static void sleepTestBoot() {
  if (!wokeFromStandby()) {
    const esp_partition_t* p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "spiffs");
    if (p) esp_partition_erase_range(p, 0, 4096);
    stMagic = 0;
  }
}
static void sleepTest() {
  static int stage = 0;
  static uint32_t t0 = millis();
  if (stage == 0 && !wokeFromStandby()) {
    if (ntpState() == NTP_IDLE && millis() > 3000) ntpStart();
    if (ntpState() == NTP_OK) {
      stNote(0, "boot1 synced t=%.3f trusted=%d", nowSec(), (int)timeTrusted());
      stSleepAt = nowSec();
      stMagic = 0x51ee7;
      stage = 9;
      standbyEnter(300);
    } else if (millis() - t0 > 45000) {
      stNote(0, "boot1 ntp failed state=%d ssid=%d", ntpState(), (int)(S.ssid[0] != 0));
      stage = 99;
    }
  }
  if (stage == 0 && wokeFromStandby()) {
    stWakeDev = nowSec();  // already corrected by standbyCorrectClock()
    stWakeUs = micros();
    stNote(1, "wake cause=%d ext1=0x%llx state=%d magic=%d devNow=%.3f slept(dev)=%.1f", (int)esp_sleep_get_wakeup_cause(),
           (unsigned long long)esp_sleep_get_ext1_wakeup_status(), timeState(), (int)(stMagic == 0x51ee7), stWakeDev,
           stWakeDev - stSleepAt);
    stage = 1;
    if (ntpState() != NTP_BUSY) ntpStart();
  }
  if (stage == 1) {
    if (ntpState() == NTP_OK) {
      double truth = nowSec();
      double expect = stWakeDev + (micros() - stWakeUs) * 1e-6;
      double off = truth - expect;
      double slept = (stWakeDev + off) - stSleepAt;  // true elapsed from sleep to wake (awake parts are exact)
      stNote(2, "after correction: offset=%.3f s over %.1f s asleep -> %.0f ppm = %.1f s/day (clock %s)", off, slept,
             off / slept * 1e6, off / slept * 86400, off > 0 ? "slow" : "fast");
      stage = 2;
    } else if (millis() - t0 > 45000) {
      stNote(2, "resync failed state=%d", ntpState());
      stage = 2;
    }
  }
}
#endif

// Hold G0 for standby: a ring fills around the button's corner; at full it goes to sleep.
static void standbyHold() {
  float h = g0Held();
  if (h < 0.35f) return;
  if (cur == APP_USB) return;
  powerActivity();
  if (pomo::phase() != pomo::IDLE) {
    static uint32_t warned = 0;
    if (millis() - warned > 3000) toast("番茄钟计时中", col::CINNABAR), warned = millis();
    return;
  }
  float k = clamp01((h - 0.35f) / 1.15f);
  if (screenAwake()) {
    darkenRect(0, 0, SW, SH, 1.0f - 0.5f * k);
    int cx = SW / 2, cy = SH / 2 - 6;
    cv.fillArc(cx, cy, 22, 19, 0, 360, col::INK3);
    cv.fillArc(cx, cy, 22, 19, -90, -90 + 360 * k, col::GOLD);
    glowDot(cx, cy, 30, col::GOLD, 0.2f * k);
    // a crescent moon in the middle
    cv.fillCircle(cx, cy, 8, col::BONE);
    cv.fillCircle(cx + 4, cy - 3, 7, scale565(col::INK, 1.0f));
    text("待机", cx, cy + 28, &F12, blend565(col::MUTE, col::BONE, k), AC);
  }
  if (k >= 1) {
    present();
    standbyEnter();
  }
}

void setup() {
  standbyBootFixup();
#ifdef SLEEPTEST
  sleepTestBoot();
#endif
  auto cfg = M5.config();
  cfg.internal_imu = true;
  cfg.internal_mic = true;
  cfg.internal_spk = true;
  M5Cardputer.begin(cfg, true);
  settingsLoad();
  gfxInit();
  setBacklightNow();
  inputInit();
  timeInit();
  standbyCorrectClock();
  sfxInit();
  M5.Speaker.begin();  // without this, sound effects stay silent until a recording restarts it
  M5.Speaker.setVolume(S.volume);
  rec::sdBegin();
  pomo::begin();
#ifdef SELFTEST
  note(13, "setup heap=%lu big=%lu", (unsigned long)ESP.getFreeHeap(), (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
#endif

  apps[APP_HOME] = makeHome();
  apps[APP_LIUREN] = makeLiuren();
  apps[APP_QIAN] = makeQian();
  apps[APP_JIAO] = makeJiao();
  apps[APP_DICE] = makeDice();
  apps[APP_COIN] = makeCoin();
  apps[APP_REC] = makeRecorder();
  apps[APP_LIB] = makeLibrary();
  apps[APP_SET] = makeSettings();
  apps[APP_USB] = makeUsb();
  apps[APP_POMO] = makePomo();
  apps[APP_KINDLE] = makeKindle();
  apps[cur]->enter();

  // Sync only when the clock actually stopped (a real power-off). After standby or a restart the RTC
  // kept counting, and WiFi would only sit on RAM the recorder may need the moment you wake it.
  if (S.ssid[0] && S.ntpMode >= 1 && !timeTrusted()) ntpStart();
  lastUs = micros();
}

#ifdef SELFTEST
#include <SD.h>
#include "esp_partition.h"
// Build with -DSELFTEST: plays a sound, records 4 s, analyses the file and writes one line per
// stage into the spiffs partition (read back with esptool read_flash).
RTC_NOINIT_ATTR static uint32_t bootCount;
static void note(int slot, const char* fmt, ...) {
  static const esp_partition_t* p = nullptr;
  static bool erased = false;
  if (!p) p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "spiffs");
  if (!p) return;
  if (!erased) {
    esp_partition_erase_range(p, 0, 4096);
    erased = true;
  }
  char line[200] = {0};
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line, sizeof line - 1, fmt, ap);
  va_end(ap);
  strcat(line, "\n");
  esp_partition_write(p, slot * 200, line, sizeof line);
}
static void selfTest() {
  static int stage = -1;
  static uint32_t t0 = 0;
  uint32_t ms = millis();
  auto heap = []() { return (unsigned long)ESP.getFreeHeap(); };
  auto big = []() { return (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT); };
  if (stage == -1) {
    bootCount = (esp_reset_reason() == ESP_RST_POWERON) ? 1 : bootCount + 1;
    note(0, "boot reason=%d count=%lu heap=%lu big=%lu", (int)esp_reset_reason(), (unsigned long)bootCount, heap(), big());
    stage = 0;
  }
  if (stage == 0 && ms > 2000) note(14, "2s heap=%lu wifi=%d ntp=%d", heap(), (int)wifiOn(), ntpState());
  if (stage == 0 && ms > 30000) {
    note(15, "30s heap=%lu big=%lu wifi=%d ntp=%d", heap(), big(), (int)wifiOn(), ntpState());
    go(APP_KINDLE);
    stage = 10;
    t0 = ms;
  }
  if (stage == 10 && ms - t0 > 6000) {
    note(5, "in kindle state=%d on=%d heap=%lu big=%lu", (int)kclick::state(), (int)kclick::on(), heap(), big());
    go(APP_HOME);
    note(6, "left kindle on=%d heap=%lu big=%lu", (int)kclick::on(), heap(), big());
    sfxChime(1.0f, 0.6f);
    stage = 1;
    t0 = ms;
  }
  if (stage == 1 && ms - t0 > 1000) {
    quickRecord(0);
    t0 = ms;
    stage = 2;
    note(1, "start state=%d err='%s' heap=%lu big=%lu", (int)rec::state(), rec::lastError(), heap(), big());
  }
  if (stage == 2 && ms - t0 > 4000) {
    note(2, "before stop state=%d bytes=%lu peak=%.2f heap=%lu big=%lu", (int)rec::state(),
         (unsigned long)rec::dataBytes(), rec::peakLevel(), heap(), big());
    rec::stop();
    stage = 3;
    t0 = ms;
  }
  if (stage == 3 && ms - t0 > 6000) {
    note(3, "stop timeout state=%d", (int)rec::state());
    stage = 5;
  }
  if (stage == 3 && rec::state() == rec::IDLE) {
    stage = 4;
    const char* path = rec::path();
    note(3, "finished %s ms=%lu overruns=%lu spk=%d heap=%lu", path, (unsigned long)rec::elapsedMs(),
         (unsigned long)rec::overruns(), (int)M5.Speaker.isRunning(), heap());
    rec::sdLock();
    File f = SD.open(path);
    rec::WavInfo w;
    if (f && rec::readWavInfo(f, w)) {
      static int16_t b[4000];
      f.seek(w.dataOffset + w.dataBytes / 2);
      int got = f.read((uint8_t*)b, sizeof b) / 2;
      int mn = 32767, mx = -32768, ch = 0;
      for (int i = 0; i < got; ++i) {
        mn = std::min(mn, (int)b[i]), mx = std::max(mx, (int)b[i]);
        if (i && b[i] != b[i - 1]) ++ch;
      }
      note(4, "wav rate=%lu bytes=%lu min=%d max=%d changes=%d of %d", (unsigned long)w.rate,
           (unsigned long)w.dataBytes, mn, mx, ch, got);
      f.close();
    } else note(4, "cannot read wav");
    rec::sdUnlock();
  }
}
#endif

#ifdef PERFTEST
#include "esp_partition.h"
// Build with -DPERFTEST: walks through the 3D scenes, throws, and writes frame timings to spiffs.
extern uint16_t g_fakeHeld;
static uint32_t pN = 0, pDraw = 0, pPush = 0, pMax = 0;
static void perfAdd(uint32_t draw, uint32_t push) {
  ++pN;
  pDraw += draw;
  pPush += push;
  pMax = std::max(pMax, draw);
}
static void perfNote(int slot, const char* name) {
  static const esp_partition_t* p = nullptr;
  if (!p) {
    p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "spiffs");
    if (p) esp_partition_erase_range(p, 0, 8192);
  }
  char line[128] = {0};
  snprintf(line, sizeof line - 1, "%-12s frames=%lu draw avg=%lu max=%lu us push avg=%lu us heap=%lu\n", name,
           (unsigned long)pN, (unsigned long)(pN ? pDraw / pN : 0), (unsigned long)pMax,
           (unsigned long)(pN ? pPush / pN : 0), (unsigned long)ESP.getFreeHeap());
  if (p) esp_partition_write(p, slot * 128, line, sizeof line);
  pN = pDraw = pPush = pMax = 0;
}
static void perfTest() {
  struct Step { uint32_t at; int what; };
  // what: 0 go app, 1 note idle, 2 hold space, 3 release, 4 note throw, 5 key '6'
  static const struct { uint32_t ms; int op; int arg; const char* name; } plan[] = {
      {2000, 0, APP_HOME, nullptr},   {5000, 1, 0, "home"},
      {5100, 0, APP_DICE, nullptr},   {5300, 5, '6', nullptr},     {8000, 1, 1, "dice6 idle"},
      {8000, 2, 0, nullptr},          {9000, 3, 0, nullptr},       {9000, 1, 2, "dice6 shake"},
      {12000, 1, 3, "dice6 roll"},
      {12100, 0, APP_JIAO, nullptr},  {15000, 1, 4, "jiao idle"},  {15000, 2, 0, nullptr},
      {15800, 3, 0, nullptr},         {15800, 1, 5, "jiao charge"}, {18500, 1, 6, "jiao fly"},
      {18600, 0, APP_COIN, nullptr},  {21000, 1, 7, "coin idle"},  {21000, 2, 0, nullptr},
      {21800, 3, 0, nullptr},         {21800, 1, 8, "coin charge"}, {24500, 1, 9, "coin flip"},
      {24600, 0, APP_QIAN, nullptr},  {27000, 1, 10, "qian idle"}, {27000, 2, 0, nullptr},
      {29000, 3, 0, nullptr},         {29000, 1, 11, "qian shake"}, {32000, 1, 12, "qian fall"},
      {32100, 0, APP_LIUREN, nullptr}, {34000, 1, 13, "liuren"},
  };
  static int i = 0;
  uint32_t ms = millis();
  while (i < (int)(sizeof plan / sizeof plan[0]) && ms >= plan[i].ms) {
    auto& s = plan[i++];
    if (s.op == 0) go((AppId)s.arg);
    if (s.op == 1) perfNote(s.arg, s.name);
    if (s.op == 2) g_fakeHeld = ' ';
    if (s.op == 3) g_fakeHeld = 0;
    if (s.op == 5) {
      KeyEvent e{};
      e.key = s.arg;
      apps[cur]->key(e);
    }
    if (s.op == 0) pN = pDraw = pPush = pMax = 0;
  }
}
#endif

void loop() {
#ifdef SELFTEST
  selfTest();
#endif
#ifdef SLEEPTEST
  sleepTest();
#endif
  uint32_t nowUs = micros();
  float dt = (nowUs - lastUs) * 1e-6f;
  lastUs = nowUs;
  if (dt > 0.1f) dt = 0.1f;

  M5Cardputer.update();
  inputPoll();
  timeTick();
  rec::service();
  kclick::service();

  char path[48];
  uint32_t ms;
  if (rec::takeFinished(path, sizeof path, &ms)) {
    char msg[48];
    snprintf(msg, sizeof msg, "已保存 %lu:%02lu", (unsigned long)(ms / 60000), (unsigned long)(ms / 1000 % 60));
    toast(msg, col::JADE);
  }

  if (S.usbAuto && usbdrive::hostArrived() && rec::state() == rec::IDLE && cur != APP_USB) {
    powerActivity();
    openUsbDrive(true);
  }

  pomo::Phase ended;
  if (pomo::tick(&ended)) {
    powerActivity();  // light the screen for the change of phase
    sfxChime(1.0f, 0.8f);
    sfxChime(1.5f, 0.6f);
    toast(ended == pomo::FOCUS ? "专注完成 · 休息一下" : S.pomoAuto ? "休息结束 · 开始专注" : "休息结束 · 空格开始",
          ended == pomo::FOCUS ? col::GOLD : col::JADE);
  }

  KeyEvent e;
  while (inputNext(e)) {
    // the first key only wakes a dark screen, except where keys work blind (page turner)
    if (powerActivity() && !apps[cur]->keysInDark()) continue;
    if (globalKey(e)) continue;
    if (apps[cur]->key(e)) continue;
    if ((e.key == K_ESC || e.key == K_BACK) && cur != APP_HOME) go(APP_HOME);
  }
  if (shakeLevel() > 0.6f && rec::state() == rec::IDLE) powerActivity();
  if (g0Held() > 0.15f && !screenAwake()) powerActivity();  // holding G0 in the dark: show the standby ring
  powerTick(dt);

  if (screenAwake()) {
#ifdef PERFTEST
    uint32_t a = micros();
#endif
    apps[cur]->frame(dt);
    recDot();
    drawToast(dt);
    standbyHold();
#ifdef PERFTEST
    uint32_t b = micros();
#endif
    present();
#ifdef PERFTEST
    perfAdd(b - a, micros() - b);
#endif
  }
#ifdef PERFTEST
  perfTest();
#endif

  uint32_t budget = 1000000 / (screenAwake() ? apps[cur]->fps() : 10);
  uint32_t spent = micros() - nowUs;
  if (spent < budget) delay((budget - spent) / 1000);
  else delay(1);
}
