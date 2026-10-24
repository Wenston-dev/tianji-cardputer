// 录音: big timer, a scrolling mirrored waveform and a breathing record button.
// While recording the screen dims by itself (see powerTick); the frame rate drops with it.
#include "app.h"
#include "rec.h"
#include "sfx.h"
#include "system.h"

namespace {

constexpr int NB = 78;  // waveform bars

class Recorder : public App {
 public:
  void enter() override {
    t = 0;
    rec::sdBegin();
  }
  bool drawsStatus() override { return true; }
  uint8_t fps() override { return 30; }  // the loop already idles when the screen is dark

  bool key(const KeyEvent& e) override {
    rec::State s = rec::state();
    if (s == rec::IDLE) {
      if (e.key == K_UP) sel = (sel + 2) % 3, sfxTick(1.2f, 0.3f);
      else if (e.key == K_DOWN) sel = (sel + 1) % 3, sfxTick(1.2f, 0.3f);
      else if (e.key >= '1' && e.key <= '3') start(e.key - '1');
      else if (e.key == K_ENTER || e.key == ' ') start(sel);
      else if (e.key == 'f') go(APP_LIB);
      else return false;
      return true;
    }
    if (s == rec::STOPPING) return true;
    if (e.key == K_ENTER) {
      rec::stop();
      return true;
    }
    if (e.key == ' ') {
      rec::togglePause();
      return true;
    }
    if (e.key == 'm') {
      rec::mark();
      markFlash = 1;
      return true;
    }
    return false;  // ESC goes home; the recording keeps running
  }

  void frame(float dt) override {
    t += dt;
    dtLast = dt;
    markFlash = std::max(0.0f, markFlash - dt * 2);
    cv.fillSprite(col::INK);
    statusBar(false, false);
    rec::State s = rec::state();
    level = approach(level, rec::peakLevel(), 18, dt);
    if (s == rec::IDLE) drawIdle();
    else drawLive(s);
  }

 private:
  float t = 0, level = 0, markFlash = 0, head = 0, dtLast = 0;
  int sel = 0;

  void start(int preset) {
    if (!rec::start(preset)) toast(rec::lastError(), col::CINNABAR);
  }

  void drawIdle() {
    // record button
    float p = 0.5f + 0.5f * sinf(t * 2.2f);
    int cx = 58, cy = 66;
    glowDot(cx, cy, 40, col::CINNABAR, 0.12f + 0.1f * p);
    cv.drawCircle(cx, cy, 30, blend565(col::INK, col::CINNABAR, 0.45f));
    cv.drawCircle(cx, cy, 31, blend565(col::INK, col::CINNABAR, 0.2f));
    cv.fillCircle(cx, cy, 20 + (int)(p * 2), col::CINNABAR);
    // tick ring
    for (int i = 0; i < 60; ++i) {
      float a = i * PI / 30 - t * 0.2f;
      float r0 = 35, r1 = (i % 5 == 0) ? 39 : 37;
      cv.drawLine(cx + cosf(a) * r0, cy + sinf(a) * r0, cx + cosf(a) * r1, cy + sinf(a) * r1, col::INK3);
    }
    // presets
    for (int i = 0; i < 3; ++i) {
      int y = 34 + i * 24;
      bool on = i == sel;
      if (on) roundPanel(112, y - 3, 122, 20, 6, col::INK2, col::GOLD2);
      char k[4];
      snprintf(k, sizeof k, "%d", i + 1);
      text(k, 122, y, &F12, on ? col::GOLD : col::MUTE, AC);
      text(rec::kPresets[i].name, 134, y, &F12, on ? col::BONE : col::DIM);
      char d[16];
      uint32_t r = kRates[rec::kPresets[i].rateIdx >= 0 ? rec::kPresets[i].rateIdx : S.rateIdx];
      snprintf(d, sizeof d, "%gk", r / 1000.0f);
      text(d, 228, y, &F12, col::MUTE, AR);
    }
    // card space as a thin gauge instead of words
    if (!rec::sdOk()) {
      text("无 SD 卡", 58, 104, &F12, col::CINNABAR, AC);
      return;
    }
    uint64_t freeB = rec::sdFreeBytes();
    float hours = freeB / (kRates[S.rateIdx] * 2.0f * 3600);
    char b[24];
    if (hours >= 1) snprintf(b, sizeof b, "%.0f 小时", hours);
    else snprintf(b, sizeof b, "%.0f 分钟", hours * 60);
    text(b, 58, 106, &F12, col::MUTE, AC);
  }

  void drawLive(rec::State s) {
    bool paused = s == rec::PAUSED;
    bool stopping = s == rec::STOPPING;
    // timer
    uint32_t sec = rec::elapsedMs() / 1000;
    char b[24];
    if (sec >= 3600) snprintf(b, sizeof b, "%lu:%02lu:%02lu", (unsigned long)(sec / 3600), (unsigned long)(sec / 60 % 60), (unsigned long)(sec % 60));
    else snprintf(b, sizeof b, "%02lu:%02lu", (unsigned long)(sec / 60), (unsigned long)(sec % 60));
    bool blink = (millis() / 500) & 1;
    uint16_t tc = paused ? (blink ? col::DIM : col::MUTE) : col::WHITE;
    textMono(b, SW / 2, 14, &FBIG, tc, 19, AC);

    // the red dot breathes with the input level
    int dx = 14, dy = 34;
    if (!paused && !stopping) {
      glowDot(dx, dy, 8 + level * 12, col::CINNABAR, 0.3f + level * 0.5f);
      cv.fillCircle(dx, dy, 4, col::CINNABAR);
    } else {
      cv.fillRect(dx - 4, dy - 4, 3, 9, col::DIM);
      cv.fillRect(dx + 1, dy - 4, 3, 9, col::DIM);
    }

    // Mirrored waveform scrolling at a steady speed. Audio arrives in 128 ms bursts, so the view
    // runs on its own clock a few entries behind the newest data and glides between entries.
    uint32_t total = rec::historyTotal();
    float rateE = rec::historyRate();
    if (!paused) head += dtLast * rateE;
    float lag = total - head;
    if (lag > 10 || lag < 1) head = total - 6.0f;  // resync after pauses or stalls
    int cy = 88, maxH = 26;
    const float pitch = 3;
    float frac = head - floorf(head);
    int newest = (int)floorf(head);
    for (int i = 0; i < NB + 1; ++i) {
      int idx = newest - i;
      if (idx < 0) break;
      float x = SW - 6 - (i + frac) * pitch;
      if (x < 0) break;
      float v = rec::historyAt(idx) / 255.0f;
      float h = std::max(1.0f, v * v * maxH);
      float age = 1 - (float)i / NB;  // older bars fade toward the left
      uint16_t c = paused ? col::INK3 : blend565(col::GOLD2, v > 0.8f ? col::EMBER : col::GOLD, v);
      c = blend565(col::INK, c, 0.2f + 0.8f * age);
      cv.fillRoundRect((int)x, (int)(cy - h), 2, (int)(h * 2) + 1, 1, c);
    }
    if (!paused) glowDot(SW - 6, cy, 10 + level * 10, col::GOLD, 0.15f + level * 0.3f);
    cv.drawFastHLine(0, cy, SW, blend565(col::INK, col::GOLD2, 0.35f));

    // markers flash a vertical line
    if (markFlash > 0) cv.drawFastVLine(SW - 6, cy - maxH - 4, maxH * 2 + 8, blend565(col::INK, col::CINNABAR, markFlash));
    for (int i = 0; i < std::min(rec::markCount(), 12); ++i) cv.fillCircle(SW - 8 - i * 5, 122, 1, col::CINNABAR);

    // size and rate, quiet
    float mb = rec::dataBytes() / 1048576.0f;
    snprintf(b, sizeof b, "%.1fMB", mb);
    text(b, 6, 117, &F12, col::MUTE);
    if (rec::overruns()) text("!", 70, 117, &F12, col::CINNABAR);
    if (stopping) text("保存中", SW / 2, 117, &F12, col::GOLD, AC);
  }
};

}  // namespace

App* makeRecorder() { return new Recorder(); }
