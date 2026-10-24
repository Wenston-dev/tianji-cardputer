// U盘模式: the SD card appears on the computer. Data flowing to and from it is drawn as light.
#include "app.h"
#include "sfx.h"
#include "usbdrive.h"

namespace {

struct Dot {
  float x, y, v, life;
  bool out;
};

class UsbApp : public App {
 public:
  bool ask = false;

  void enter() override {
    t = 0;
    askT = 0;
    exitArm = 0;
    goneT = 0;
    ejT = 0;
    wasMounted = false;
    failed = false;
    for (auto& d : dots) d.life = 0;
    if (!ask) start();
  }

  bool key(const KeyEvent& e) override {
    if (ask) {  // any key cancels the countdown
      ask = false;
      go(APP_HOME);
      return true;
    }
    if (!usbdrive::active()) return false;
    if (e.key == K_ESC || e.key == K_BACK) {
      if (exitArm > 0 || usbdrive::ejected()) usbdrive::exitAndRestart();
      exitArm = 2.5f;
      return true;
    }
    return true;
  }

  void frame(float dt) override {
    t += dt;
    exitArm = std::max(0.0f, exitArm - dt);
    if (ask) {
      askT += dt;
      if (askT >= 3) {
        ask = false;
        start();
      }
    }
    if (usbdrive::active()) {
      if (usbdrive::mounted()) wasMounted = true;
      bool gone = wasMounted && (!usbdrive::mounted() || usbdrive::suspended());
      goneT = gone ? goneT + dt : 0;
      if (goneT > 1.5f) usbdrive::exitAndRestart();  // cable pulled: come back as ourselves
      if (usbdrive::ejected()) {
        ejT += dt;
        if (ejT > 1.6f) usbdrive::exitAndRestart();  // ejected on the computer: back to normal
      }
      flow(dt);
    }
    draw();
  }

 private:
  float t = 0, askT = 0, exitArm = 0, goneT = 0, rate = 0, ejT = 0;
  bool wasMounted = false, failed = false;
  uint32_t lastR = 0, lastW = 0;
  float acc = 0;
  Dot dots[40];

  void start() {
    failed = !usbdrive::begin();
    if (!failed) sfxChime(1.2f, 0.5f);
  }

  void flow(float dt) {
    uint32_t r = usbdrive::bytesRead(), w = usbdrive::bytesWritten();
    float inst = ((r - lastR) + (w - lastW)) / std::max(dt, 0.001f);
    bool out = (r - lastR) >= (w - lastW);
    lastR = r;
    lastW = w;
    rate = approach(rate, inst, 3, dt);
    // more traffic, more light
    acc += std::min(60.0f, inst / 20000.0f) * dt;
    while (acc >= 1) {
      acc -= 1;
      for (auto& d : dots)
        if (d.life <= 0) {
          d = {out ? 150.0f : 20.0f, 64 + frand(-14, 14), frand(70, 120), 1.4f, out};
          break;
        }
    }
    for (auto& d : dots) {
      if (d.life <= 0) continue;
      d.life -= dt;
      d.x += (d.out ? -1 : 1) * d.v * dt;
    }
  }

  void card(int cx, int cy, uint16_t c) {
    int w = 34, h = 42, x = cx - w / 2, y = cy - h / 2;
    cv.fillRoundRect(x, y, w, h, 4, col::INK2);
    cv.drawRoundRect(x, y, w, h, 4, c);
    cv.fillTriangle(x + w - 10, y, x + w, y, x + w, y + 10, col::INK);
    cv.drawLine(x + w - 10, y, x + w, y + 10, c);
    for (int i = 0; i < 4; ++i) cv.fillRect(x + 6 + i * 5, y + 5, 3, 8, blend565(col::INK, c, 0.6f));
  }

  void laptop(int cx, int cy, uint16_t c) {
    cv.drawRoundRect(cx - 18, cy - 14, 36, 24, 3, c);
    cv.fillRect(cx - 24, cy + 11, 48, 3, c);
  }

  void draw() {
    cv.fillSprite(col::INK);
    int cardX = 168, pcX = 44, cy = 62;
    float p = 0.5f + 0.5f * sinf(t * 2.5f);
    if (ask) {
      // the countdown ring fills around the card
      float k = clamp01(askT / 3);
      card(cardX, cy, col::GOLD);
      laptop(pcX, cy, col::DIM);
      cv.fillArc(cardX, cy, 32, 30, -90, -90 + 360 * k, col::GOLD);
      text("U 盘模式", SW / 2, 96, &F16, col::BONE, AC);
      text("任意键取消", SW / 2, 119, &F12, col::MUTE, AC);
      return;
    }
    if (failed) {
      card(cardX, cy, col::CINNABAR);
      text("无法进入", SW / 2, 96, &F16, col::CINNABAR, AC);
      text("请检查 SD 卡", SW / 2, 119, &F12, col::MUTE, AC);
      return;
    }
    bool ej = usbdrive::ejected();
    uint16_t c = ej ? col::JADE : col::GOLD;
    // link line between computer and card
    for (int x = pcX + 26; x < cardX - 22; x += 4) cv.drawPixel(x, cy, usbdrive::mounted() ? col::LINE : col::INK3);
    for (auto& d : dots) {
      if (d.life <= 0 || d.x < pcX + 24 || d.x > cardX - 20) continue;
      glowDot(d.x, cy + (d.y - 64) * 0.15f, 4, d.out ? col::GOLD : col::SKY, 0.6f * clamp01(d.life));
    }
    laptop(pcX, cy, usbdrive::mounted() ? col::BONE : col::MUTE);
    glowDot(cardX, cy, 30, c, (ej ? 0.15f : 0.12f + 0.1f * p));
    card(cardX, cy, c);
    if (ej) {
      cv.drawLine(cardX - 7, cy + 2, cardX - 2, cy + 7, col::JADE);
      cv.drawLine(cardX - 2, cy + 7, cardX + 8, cy - 5, col::JADE);
      text("可以拔出了", SW / 2, 104, &F16, col::JADE, AC);
    } else if (!usbdrive::mounted()) {
      text("等待电脑识别", SW / 2, 104, &F12, col::DIM, AC);
    } else if (rate > 2048) {
      char b[24];
      snprintf(b, sizeof b, "%.1f MB/s", rate / 1048576.0f);
      textMono(b, SW / 2, 104, &F12, col::DIM, 7, AC);
    }
    if (exitArm > 0) text("再按 Esc 退出", SW / 2, 121, &F12, col::CINNABAR, AC);
  }
};

UsbApp* usbApp = nullptr;

}  // namespace

App* makeUsb() { return usbApp = new UsbApp(); }

void openUsbDrive(bool ask) {
  if (!usbApp || currentApp() == APP_USB) return;
  usbApp->ask = ask;
  go(APP_USB);
}
