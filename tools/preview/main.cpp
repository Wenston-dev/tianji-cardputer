// Host preview: run screens with a virtual clock and dump frames as images.
#include <deque>
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
static void onSig(int){ void* b[40]; int n=backtrace(b,40); backtrace_symbols_fd(b,n,2); _exit(1);}
#include <map>
#include "app.h"
#include "rec.h"
#include "system.h"
#include "pomo.h"
#include "kclick.h"

extern uint64_t g_us;
extern double g_epoch0;
extern std::deque<KeyEvent> g_keys;
extern std::vector<uint16_t> g_held;
extern float g_shake;
namespace rec { extern State g_state; extern uint32_t g_elapsed; }
namespace kclick { extern int g_state; }

static App* apps[APP_COUNT];
static AppId cur = APP_HOME;
void go(AppId id) { apps[cur]->leave(); cur = id; apps[cur]->enter(); }
AppId currentApp() { return cur; }
void quickRecord(int p) { rec::start(p); go(APP_REC); }
#ifndef HAVE_ALL
void openUsbDrive(bool) {}
#endif

static std::string outDir = "out";
static void snap(const std::string& name) {
  std::string p = outDir + "/" + name + ".ppm";
  FILE* f = fopen(p.c_str(), "wb");
  fprintf(f, "P6 %d %d 255\n", SW, SH);
  uint16_t* b = (uint16_t*)cv.getBuffer();
  for (int i = 0; i < SW * SH; ++i) {
    uint16_t c = __builtin_bswap16(b[i]);
    uint8_t px[3] = {(uint8_t)(((c >> 11) & 31) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31)};
    fwrite(px, 1, 3, f);
  }
  fclose(f);
}

static void sim(float secs, float fps = 30) {
  int n = (int)(secs * fps);
  float dt = 1.0f / fps;
  for (int i = 0; i < n; ++i) {
    g_us += (uint64_t)(dt * 1e6);
    KeyEvent e;
    while (!g_keys.empty()) {
      e = g_keys.front();
      g_keys.pop_front();
      if (!apps[cur]->key(e) && (e.key == K_ESC || e.key == K_BACK) && cur != APP_HOME) go(APP_HOME);
    }
    apps[cur]->frame(dt);
    drawToast(dt);
  }
}

static void press(uint16_t k, char ch = 0, bool shift = false) {
  KeyEvent e{};
  e.key = k;
  e.ch = ch ? ch : (k < 0x100 ? (char)k : 0);
  e.shift = shift;
  g_keys.push_back(e);
}
static void type(const char* s) { for (; *s; ++s) press((uint8_t)tolower(*s), *s); }

int main(int argc, char** argv) {
  if (argc > 1) outDir = argv[1];
  std::string only = argc > 2 ? argv[2] : "";
  signal(SIGTRAP,onSig); signal(SIGSEGV,onSig); signal(SIGABRT,onSig); signal(SIGILL,onSig);
  srand(7);
  // 2026-10-23 15:30:12 +08:00
  g_epoch0 = daysFromCivil(2026, 10, 23) * 86400.0 + 15 * 3600 + 30 * 60 + 12 - 8 * 3600;
  S.birthY = 1992; S.birthM = 5; S.birthD = 20; S.lifespan = 80;
  M5.Display.createSprite(SW, SH);
  gfxInit();
  apps[APP_HOME] = makeHome();
  apps[APP_LIUREN] = makeLiuren();
#ifdef HAVE_ALL
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
#endif
  for (int i = 0; i < APP_COUNT; ++i) if (!apps[i]) apps[i] = apps[APP_HOME];
  auto want = [&](const char* n) { return only.empty() || only == n; };

  if (want("home")) {
    cur = APP_HOME; apps[cur]->enter();
    sim(1.0f); snap("home_1");
    sim(9.5f); snap("home_2");
    press(K_ENTER); sim(0.5f); press(K_RIGHT); press(K_RIGHT); sim(0.6f); snap("home_dock");
    press(K_ESC); sim(0.5f);
  }
  if (want("liuren")) {
    go(APP_LIUREN); sim(0.5f); snap("liuren_idle");
    press(' '); sim(1.2f); snap("liuren_run");
    sim(6.0f); snap("liuren_done");
    press(K_ENTER); sim(3.5f); snap("liuren_verse");
    press(K_ENTER); press(K_TAB); type("37 8 251"); sim(0.3f); snap("liuren_num");
    go(APP_HOME);
  }
#ifdef HAVE_ALL
#include "scenarios.inc"
#endif
  return 0;
}
