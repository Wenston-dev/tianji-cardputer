// 翻页器: the Cardputer as a Kindle Click remote. A little e-reader turns its page with every key.
// The screen goes dark while you read; keys keep turning pages in the dark.
#include "app.h"
#include "kclick.h"
#include "sfx.h"
#include "system.h"
#include "rec.h"
#include <M5Cardputer.h>

namespace {

class Kindle : public App {
 public:
  // Bluetooth only runs while this screen is open: its stack needs ~65 KB, so the speaker and the
  // sound-effect buffers step aside for it and come back on the way out.
  void enter() override {
    flipT = 1;
    armT = 0;
    needRestart = false;
    if (S.ble && rec::state() == rec::IDLE) {
      ntpAbort();  // WiFi and Bluetooth stacks don't fit in RAM together
      M5.Speaker.end();
      sfxRelease();
      if (!kclick::begin()) {
        needRestart = true;
        M5.Speaker.begin();
        sfxInit();
      }
    }
    kclick::wake();
    powerAppDark(20);
  }
  void leave() override {
    powerAppDark(0);
    if (kclick::on()) {
      kclick::end();
      M5.Speaker.begin();
      M5.Speaker.setVolume(S.volume);
      sfxInit();
    }
  }
  bool keysInDark() override { return true; }

  bool key(const KeyEvent& e) override {
    if (e.key == K_ESC || e.key == K_BACK) return false;
    if (needRestart && e.key == K_ENTER) ESP.restart();
    if (!kclick::on()) return true;
    kclick::Key k;
    if (e.key == ' ' || e.key == K_ENTER || e.key == K_RIGHT || e.key == K_DOWN || e.key == 'n') k = kclick::NEXT;
    else if (e.key == K_LEFT || e.key == K_UP || e.key == 'b') k = kclick::PREV;
    else if (e.key == '=') k = kclick::BRI_UP;
    else if (e.key == '-') k = kclick::BRI_DOWN;
    else if (e.key == 'z') k = kclick::SLEEP;
    else if (e.key == 'f') {
      if (armT > 0) {
        kclick::forget();
        armT = 0;
        toast("已清除配对，请在 Kindle 上忘记设备", col::GOLD);
      } else armT = 3;
      return true;
    } else return true;
    if (e.repeat && (k == kclick::NEXT || k == kclick::PREV || k == kclick::SLEEP)) return true;
    bool sent = kclick::tap(k);
    if (k == kclick::NEXT || k == kclick::PREV) {
      flipDir = k == kclick::NEXT ? 1 : -1;
      flipT = 0;
      if (sent) sfxTick(k == kclick::NEXT ? 1.3f : 1.0f, 0.25f);
    }
    return true;
  }

  void frame(float dt) override {
    t += dt;
    flipT += dt;
    armT = std::max(0.0f, armT - dt);
    cv.fillSprite(col::INK);
    kclick::State st = kclick::state();
    drawReader(st);
    drawLink(st);
  }

 private:
  float t = 0, flipT = 1, armT = 0;
  int flipDir = 1;
  bool needRestart = false;

  // A small e-reader: bezel, paper screen with text lines, and a page that curls across on a turn.
  void drawReader(kclick::State st) {
    int x = 26, y = 16, w = 74, h = 104;
    cv.fillRoundRect(x, y, w, h, 8, rgb(26, 26, 30));
    cv.drawRoundRect(x, y, w, h, 8, col::LINE);
    int sx = x + 7, sy = y + 8, sw = w - 14, sh = h - 22;
    uint16_t paper = st == kclick::READY ? rgb(222, 218, 206) : rgb(150, 148, 140);
    cv.fillRect(sx, sy, sw, sh, paper);
    // text lines; the page number decides their lengths so each page looks different
    uint32_t seed = kclick::pages() * 2654435761u + 7;
    auto lines = [&](int ox, uint16_t ink) {
      uint32_t s = seed;
      for (int i = 0; i < 9; ++i) {
        s = s * 1103515245u + 12345u;
        int len = i == 8 ? 12 + (s >> 16) % 20 : sw - 10 - (s >> 16) % 8;
        cv.drawFastHLine(sx + 5 + ox, sy + 6 + i * 8, std::max(0, std::min(len, sw - 10 - ox)), ink);
      }
    };
    lines(0, blend565(paper, col::SUMI, 0.55f));
    // page curl
    float u = clamp01(flipT / 0.32f);
    if (u < 1) {
      float e = easeInOutCubic(u);
      int edge = flipDir > 0 ? sx + sw - (int)(e * sw) : sx + (int)(e * sw);
      int curl = (int)(sinf(e * PI) * 10);
      // the turning sheet: a band between the moving edge and its curl, shaded
      int a = flipDir > 0 ? edge : edge - curl, b = flipDir > 0 ? edge + curl : edge;
      a = std::max(sx, a);
      b = std::min(sx + sw, b);
      for (int xx = a; xx < b; ++xx) {
        float k = (float)(xx - a) / std::max(1, b - a);
        if (flipDir < 0) k = 1 - k;
        cv.drawFastVLine(xx, sy, sh, blend565(rgb(250, 248, 240), rgb(160, 156, 146), k));
      }
      // shadow cast on the page beneath
      int shX = flipDir > 0 ? edge - 4 : edge;
      tintRect(shX, sy, 4, sh, col::SUMI, 0.25f * sinf(e * PI));
    }
    // home bar
    cv.fillRoundRect(x + w / 2 - 8, y + h - 9, 16, 3, 1, col::LINE);
    if (st == kclick::OFF) darkenRect(x, y, w, h, 0.5f);
  }

  void drawLink(kclick::State st) {
    int x = 116;
    // Bluetooth rune with pulses that slow down as the link settles
    int bx = SW - 22, by = 24;
    uint16_t c = st == kclick::READY ? col::JADE : st == kclick::OFF ? col::MUTE : col::GOLD;
    float speed = st == kclick::PAIRING ? 1.6f : st == kclick::WAITING ? 0.8f : st == kclick::CONNECTED ? 2.4f : 0;
    if (speed > 0)
      for (int i = 0; i < 2; ++i) {
        float p = fmodf(t * speed + i * 0.5f, 1.0f);
        cv.drawCircle(bx, by, (int)(4 + p * 14), blend565(col::INK, c, (1 - p) * 0.8f));
      }
    cv.drawLine(bx - 3, by - 4, bx + 3, by + 3, c);
    cv.drawLine(bx - 3, by + 4, bx + 3, by - 3, c);
    cv.drawLine(bx, by - 7, bx, by + 7, c);
    cv.drawLine(bx, by - 7, bx + 3, by - 4, c);
    cv.drawLine(bx, by + 7, bx + 3, by + 4, c);

    static const char* label[] = {"蓝牙已关闭", "等待配对", "等待 Kindle", "连接中", "已连接"};
    text(label[st], x, 14, &F16, c);
    if (st == kclick::OFF) {
      if (needRestart) text("Enter 重启以开启", x, 40, &F12, col::GOLD);
      else if (rec::state() != rec::IDLE) text("录音时不可用", x, 40, &F12, col::MUTE);
      else text("设置 › 蓝牙翻页器", x, 40, &F12, col::MUTE);
      return;
    }
    if (st == kclick::PAIRING) {
      text("在 Kindle 上：", x, 40, &F12, col::DIM);
      text("设置 › 蓝牙设备", x, 56, &F12, col::BONE);
      text("选择 TianJi Click", x, 72, &F12, col::BONE);
    } else {
      char b[16];
      snprintf(b, sizeof b, "%lu", (unsigned long)kclick::pages());
      textMono(b, x, 38, &FBIG, st == kclick::READY ? col::WHITE : col::MUTE, 19);
      text("页", x + textW(b, &FBIG) + 4, 58, &F12, col::MUTE);
    }
    if (armT > 0) text("再按 F 清除配对", x, 104, &F12, col::CINNABAR);
    else text("←→ 翻页  -= 亮度", x, 104, &F12, col::LINE);
    text("Z 休眠  F 清除配对", x, 119, &F12, col::LINE);
  }
};

}  // namespace

App* makeKindle() { return new Kindle(); }
