// Screen ("app") interface and navigation.
#pragma once
#include "gfx.h"
#include "input.h"

struct App {
  virtual ~App() {}
  virtual void enter() {}
  virtual void leave() {}
  virtual void frame(float dt) = 0;  // update + draw into cv
  virtual bool key(const KeyEvent& e) { return false; }  // true = handled
  virtual bool drawsStatus() { return false; }
  virtual uint8_t fps() { return 30; }
  virtual bool keysInDark() { return false; }  // keys act even while the screen is dark
};

enum AppId { APP_HOME, APP_LIUREN, APP_QIAN, APP_JIAO, APP_DICE, APP_COIN, APP_REC, APP_LIB, APP_SET, APP_POMO, APP_KINDLE, APP_USB, APP_COUNT };

struct LaunchItem {
  AppId id;
  const char* name;
  char key;
  int icon;
};
static constexpr int kLaunchN = 10;
extern const LaunchItem kLaunch[kLaunchN];

void go(AppId id);
AppId currentApp();
// Start a recording from anywhere using one of the presets and jump to the recorder.
void quickRecord(int preset);
// Open the USB drive screen; ask=true shows a cancellable countdown first.
void openUsbDrive(bool ask);

App* makeHome();
App* makeLiuren();
App* makeQian();
App* makeJiao();
App* makeDice();
App* makeCoin();
App* makeRecorder();
App* makeLibrary();
App* makeSettings();
App* makeUsb();
App* makePomo();
App* makeKindle();
// Focus timer badge (ring + time left) for screens other than the timer itself.
void drawPomoBadge(int x, int y, bool withText);
