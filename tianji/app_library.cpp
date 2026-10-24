// 录音库: browse, play and delete recordings. The player shows the whole file as a waveform
// that lights up as it plays.
#include "app.h"
#include "rec.h"
#include "sfx.h"
#include "store.h"
#include "system.h"

namespace {

constexpr int ROW = 24;
constexpr int OVW = 232;

class Library : public App {
 public:
  void enter() override {
    mode = LIST;
    refresh();
    t = 0;
  }
  void leave() override {
    store::playClose();
    items.clear();
    items.shrink_to_fit();
  }

  bool key(const KeyEvent& e) override {
    if (mode == PLAY) return keyPlay(e);
    if (e.key == 'u') {
      openUsbDrive(false);
      return true;
    }
    if (items.empty()) {
      if (e.key == 'r') {
        quickRecord(0);
        return true;
      }
      return false;
    }
    if (e.key == K_UP) move(-1);
    else if (e.key == K_DOWN) move(1);
    else if (e.key == K_ENTER || e.key == ' ') open();
    else if (e.key == 'd') {
      if (armT > 0) {
        store::remove(items[sel].path);
        sfxKnock(0.7f, 0.5f);
        armT = 0;
        refresh();
        sel = std::min(sel, (int)items.size() - 1);
      } else {
        armT = 3;
        sfxTick(0.6f, 0.5f);
      }
    } else if (e.key == 'r') quickRecord(0);
    else return false;
    return true;
  }

  void frame(float dt) override {
    t += dt;
    armT = std::max(0.0f, armT - dt);
    cv.fillSprite(col::INK);
    if (mode == PLAY) {
      store::playService();
      drawPlayer(dt);
      return;
    }
    if (!items.empty() && previewFor != sel && t - selT > 0.25f) {
      previewFor = sel;
      store::overviewStart(items[sel].path, 80);
    }
    drawList(dt);
  }

 private:
  enum { LIST, PLAY } mode = LIST;
  std::vector<store::Entry> items;
  int sel = 0, previewFor = -1;
  float t = 0, selT = 0, scroll = 0, hiY = 0, armT = 0, volT = 0;
  float marks[64];
  int markN = 0;

  void refresh() {
    rec::sdLock();
    store::list(items);
    rec::sdUnlock();
    previewFor = -1;
    sel = std::max(0, std::min(sel, (int)items.size() - 1));
  }

  void move(int d) {
    int n = (int)items.size();
    sel = (sel + d + n) % n;
    selT = t;
    armT = 0;
    sfxTick(1.2f, 0.3f);
  }

  void open() {
    if (rec::state() != rec::IDLE) {
      toast("录音中，暂不能播放", col::CINNABAR);
      return;
    }
    if (!store::playOpen(items[sel].path)) {
      toast("无法打开", col::CINNABAR);
      return;
    }
    store::overviewStart(items[sel].path, OVW);
    markN = store::loadMarks(items[sel].path, marks, 64);
    mode = PLAY;
    startWipe(true);
    store::playToggle();
  }

  bool keyPlay(const KeyEvent& e) {
    float step = e.shift ? 30 : 5;
    if (e.key == ' ' || e.key == K_ENTER) store::playToggle();
    else if (e.key == K_LEFT) store::playSeek(store::playPos() - step);
    else if (e.key == K_RIGHT) store::playSeek(store::playPos() + step);
    else if (e.key == '-' || e.key == '=') {
      store::playVolume(e.key == '=' ? 16 : -16);
      volT = 1.2f;
    } else if (e.key == 'n' || e.key == 'p') {
      float now = store::playPos();
      float best = e.key == 'n' ? 1e9f : -1;
      for (int i = 0; i < markN; ++i) {
        if (e.key == 'n' && marks[i] > now + 0.5f && marks[i] < best) best = marks[i];
        if (e.key == 'p' && marks[i] < now - 1.5f && marks[i] > best) best = marks[i];
      }
      if (best >= 0 && best < 1e8f) store::playSeek(best);
    } else if (e.key == K_ESC || e.key == K_BACK) {
      store::playClose();
      mode = LIST;
      startWipe(false);
    } else return false;
    return true;
  }

  static void fmtTime(uint32_t sec, char* b, size_t n) {
    if (sec >= 3600) snprintf(b, n, "%lu:%02lu:%02lu", (unsigned long)(sec / 3600), (unsigned long)(sec / 60 % 60), (unsigned long)(sec % 60));
    else snprintf(b, n, "%02lu:%02lu", (unsigned long)(sec / 60), (unsigned long)(sec % 60));
  }

  void drawList(float dt) {
    text("录音库", 6, 2, &F12, col::DIM);
    if (items.empty()) {
      drawIcon(6, SW / 2, 58, col::MUTE, t);
      text(rec::sdOk() ? "暂无录音" : "无 SD 卡", SW / 2, 78, &F12, col::MUTE, AC);
      return;
    }
    char b[24];
    snprintf(b, sizeof b, "%d", (int)items.size());
    text(b, SW - 6, 2, &F12, col::MUTE, AR);
    int top = 20, vis = (SH - top) / ROW;
    float want = std::max(0, std::min(sel - vis / 2, (int)items.size() - vis));
    scroll = approach(scroll, want, 14, dt);
    hiY = approach(hiY, sel, 20, dt);
    // highlight slides between rows
    int hy = top + (int)((hiY - scroll) * ROW);
    bool armed = armT > 0;
    uint16_t hc = armed ? col::CINNABAR : col::GOLD;
    roundPanel(4, hy, SW - 8, ROW - 2, 5, blend565(col::INK, hc, 0.12f), blend565(col::INK, hc, 0.6f));
    cv.fillRect(4, hy + 5, 2, ROW - 12, hc);
    // mini waveform under the selected row
    if (previewFor == sel && !armed) {
      const uint8_t* ov = store::overview();
      int done = store::overviewDone();
      for (int i = 0; i < done && i < 80; ++i) {
        int h = std::max(1, ov[i] * 8 / 255);
        cv.drawFastVLine(SW - 136 + i, hy + ROW / 2 - 1 - h / 2, h, blend565(col::INK2, col::GOLD2, 0.8f));
      }
    }
    int first = (int)scroll;
    for (int i = first; i < (int)items.size() && i <= first + vis; ++i) {
      int y = top + (int)((i - scroll) * ROW);
      if (y > SH) break;
      bool on = i == sel;
      text(items[i].title, 12, y + 4, &F12, on ? col::BONE : col::DIM);
      fmtTime(items[i].ms / 1000, b, sizeof b);
      if (on && armed) text("D 确认删除", SW - 10, y + 4, &F12, col::CINNABAR, AR);
      else textMono(b, SW - 10, y + 4, &F12, on ? col::GOLD : col::MUTE, 7, AR);
    }
  }

  void drawPlayer(float dt) {
    const store::Entry& it = items[sel];
    text(it.title, 6, 2, &F12, col::DIM);
    float pos = store::playPos(), L = std::max(0.001f, store::playLen());
    float frac = clamp01(pos / L);
    const uint8_t* ov = store::overview();
    int done = store::overviewDone();
    int x0 = (SW - OVW) / 2, cy = 62, half = 28;
    int ph = x0 + (int)(frac * OVW);
    float lvl = store::playLevel();
    for (int i = 0; i < done && i < OVW; ++i) {
      float v = ov[i] / 255.0f;
      int h = std::max(1, (int)(v * half));
      int x = x0 + i;
      uint16_t c;
      if (x < ph) c = blend565(col::GOLD2, col::GOLD, v);
      else c = blend565(col::INK3, col::MUTE, v * 0.6f);
      // bars near the playhead swell with the live level
      float near = 1 - std::min(1.0f, fabsf((float)(x - ph)) / 10.0f);
      if (near > 0 && store::playing()) h = (int)(h * (1 + near * lvl * 0.6f));
      cv.drawFastVLine(x, cy - h, h * 2, c);
    }
    glowDot(ph, cy, 14 + lvl * 10, col::GOLD, store::playing() ? 0.25f + 0.3f * lvl : 0.15f);
    cv.drawFastVLine(ph, cy - half - 4, half * 2 + 8, col::WHITE);
    for (int i = 0; i < markN; ++i) {
      int x = x0 + (int)(clamp01(marks[i] / L) * OVW);
      cv.fillTriangle(x, cy + half + 4, x - 3, cy + half + 9, x + 3, cy + half + 9, col::CINNABAR);
    }
    // time
    char b[24];
    fmtTime((uint32_t)pos, b, sizeof b);
    textMono(b, 8, 106, &F16, col::BONE, 9);
    fmtTime((uint32_t)L, b, sizeof b);
    textMono(b, SW - 8, 110, &F12, col::MUTE, 7, AR);
    // play / pause glyph
    int gx = SW / 2, gy = 114;
    if (store::playing()) {
      cv.fillRect(gx - 5, gy - 6, 3, 12, col::BONE);
      cv.fillRect(gx + 2, gy - 6, 3, 12, col::BONE);
    } else {
      cv.fillTriangle(gx - 4, gy - 7, gx - 4, gy + 7, gx + 7, gy, col::BONE);
    }
    // volume flash
    volT = std::max(0.0f, volT - dt);
    if (volT > 0) {
      float a = clamp01(volT * 2);
      int vh = store::playVolumeNow() * 60 / 255;
      cv.drawRoundRect(SW - 10, 32, 5, 62, 2, blend565(col::INK, col::LINE, a));
      cv.fillRect(SW - 9, 93 - vh, 3, vh, blend565(col::INK, col::GOLD, a));
    }
  }
};

}  // namespace

App* makeLibrary() { return new Library(); }
