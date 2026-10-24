// 番茄钟: pick a preset, focus inside a slowly breathing ring, review the week on the stats page.
// The timer itself lives in pomo.cpp and keeps running when you leave this screen.
#include "app.h"
#include "pomo.h"
#include "sfx.h"
#include "system.h"

namespace {

enum View { LIST, RUN, STATS, EDIT };
constexpr int ROW = 22;

uint16_t phaseColor(pomo::Phase p) { return p == pomo::FOCUS ? col::CINNABAR : col::JADE; }

void fmtMs(uint32_t ms, char* b, size_t n) {
  uint32_t s = (ms + 999) / 1000;
  snprintf(b, n, "%02lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

// Progress ring drawn as short arc segments so the colour can glow toward the head.
void ring(int cx, int cy, int r, int w, float p, uint16_t c, float t, bool live) {
  cv.fillArc(cx, cy, r, r - w, 0, 360, col::INK3);
  int segs = (int)(p * 120);
  for (int i = 0; i < segs; ++i) {
    float a0 = -90 + i * 3, a1 = a0 + 3.2f;
    float k = (float)i / std::max(1, segs);
    cv.fillArc(cx, cy, r, r - w, a0, a1, blend565(scale565(c, 0.45f), c, k));
  }
  if (p > 0) {
    float a = (-90 + p * 360) * PI / 180;
    float breathe = live ? 0.5f + 0.5f * sinf(t * 2 * PI / 10) : 0.3f;  // six breaths a minute
    glowDot(cx + cosf(a) * (r - w / 2.0f), cy + sinf(a) * (r - w / 2.0f), 8 + 4 * breathe, c, 0.35f + 0.3f * breathe);
  }
}

class Pomo : public App {
 public:
  void enter() override {
    view = pomo::phase() != pomo::IDLE ? RUN : LIST;
    sel = std::min(sel, pomo::count() - 1);
    armT = 0;
    t = 0;
    today = pomo::todayMinutes();
  }
  void leave() override { inputTextMode(false); }
  bool drawsStatus() override { return true; }

  bool key(const KeyEvent& e) override {
    switch (view) {
      case LIST: return keyList(e);
      case RUN: return keyRun(e);
      case STATS:
        if (e.key == K_TAB || e.key == K_ESC || e.key == K_BACK) {
          view = LIST;
          return true;
        }
        return true;
      case EDIT: return keyEdit(e);
    }
    return false;
  }

  void frame(float dt) override {
    t += dt;
    armT = std::max(0.0f, armT - dt);
    cv.fillSprite(col::INK);
    if (view == RUN && pomo::phase() == pomo::IDLE) view = LIST;
    switch (view) {
      case LIST: drawList(dt); break;
      case RUN: drawRun(); break;
      case STATS: drawStats(); break;
      case EDIT: drawEdit(); break;
    }
  }

 private:
  View view = LIST;
  int sel = 0, field = 0, editIdx = 0;
  float t = 0, scroll = 0, hi = 0, armT = 0, statsT = 0;
  int today = 0;
  pomo::Preset work;
  bool typing = false;
  pomo::Day days[7];
  int streak = 0;

  bool keyList(const KeyEvent& e) {
    int rows = pomo::count() + 1;  // last row adds a preset
    if (e.key == K_UP) sel = (sel + rows - 1) % rows, sfxTick(1.2f, 0.3f), armT = 0;
    else if (e.key == K_DOWN) sel = (sel + 1) % rows, sfxTick(1.2f, 0.3f), armT = 0;
    else if (e.key == K_ENTER || e.key == ' ') {
      if (sel == pomo::count()) beginEdit(-1);
      else {
        pomo::start(sel);
        view = RUN;
        sfxChime(1.3f, 0.5f);
      }
    } else if (e.key == 'e' && sel < pomo::count()) beginEdit(sel);
    else if (e.key == 'n') beginEdit(-1);
    else if (e.key == 'd' && sel < pomo::count()) {
      if (armT > 0) {
        pomo::remove(sel);
        sel = std::min(sel, pomo::count() - 1);
        armT = 0;
        sfxKnock(0.7f, 0.5f);
      } else armT = 3;
    } else if (e.key == K_TAB) {
      pomo::stats(days, streak);
      today = days[6].minutes;
      statsT = 0;
      view = STATS;
    } else return false;
    return true;
  }

  bool keyRun(const KeyEvent& e) {
    if (e.key == ' ' || e.key == K_ENTER) {
      pomo::togglePause();
      sfxTick(pomo::paused() ? 0.8f : 1.2f, 0.5f);
    } else if (e.key == 's') {
      pomo::skip();
      sfxKnock(1.2f, 0.4f);
    } else if (e.key == 'x' || e.key == K_BACK) {
      if (armT > 0) {
        pomo::stop();
        view = LIST;
        armT = 0;
        today = pomo::todayMinutes();
      } else armT = 2.5f;
    } else return false;  // Esc goes home and the timer keeps running
    return true;
  }

  void beginEdit(int idx) {
    editIdx = idx < 0 ? pomo::count() : idx;
    if (idx < 0) {
      if (pomo::count() >= pomo::kMax) {
        toast("最多 12 个", col::CINNABAR);
        return;
      }
      work = {"Custom", 25, 5, 15, 4};
    } else work = pomo::get(idx);
    field = 0;
    typing = false;
    view = EDIT;
  }

  bool keyEdit(const KeyEvent& e) {
    if (typing) {
      size_t l = strlen(work.name);
      if (e.key == K_ENTER || e.key == K_ESC) {
        typing = false;
        inputTextMode(false);
      } else if (e.key == K_BACK) {
        if (l) {
          // drop a whole UTF-8 character
          do --l;
          while (l && ((uint8_t)work.name[l] & 0xC0) == 0x80);
          work.name[l] = 0;
        }
      } else if (e.ch >= 32 && e.ch < 127 && l < sizeof(work.name) - 1) {
        work.name[l] = e.ch;
        work.name[l + 1] = 0;
      }
      return true;
    }
    int step = e.shift ? 5 : 1;
    uint8_t* v[] = {nullptr, &work.focus, &work.shortBreak, &work.longBreak, &work.every};
    const int lo[] = {0, 1, 1, 1, 0}, hiV[] = {0, 180, 60, 90, 8};
    if (e.key == K_UP) field = (field + 4) % 5;
    else if (e.key == K_DOWN) field = (field + 1) % 5;
    else if ((e.key == K_LEFT || e.key == K_RIGHT) && field > 0) {
      int d = e.key == K_RIGHT ? step : -step;
      *v[field] = std::max(lo[field], std::min(hiV[field], *v[field] + d));
      sfxTick(1.0f + 0.05f * d, 0.3f);
    } else if (e.key == K_ENTER && field == 0) {
      typing = true;
      inputTextMode(true);
    } else if (e.key == K_ENTER || e.key == K_ESC) {
      if (!work.name[0]) strcpy(work.name, "Custom");
      pomo::set(editIdx, work);
      sel = editIdx;
      view = LIST;
      sfxChime(1.3f, 0.4f);
    } else return false;
    return true;
  }

  void drawList(float dt) {
    text("番茄钟", 6, 2, &F12, col::DIM);
    char b[32];
    snprintf(b, sizeof b, "今日 %d′", today);
    text(b, SW - 6, 2, &F12, col::GOLD, AR);
    int rows = pomo::count() + 1;
    int top = 20, vis = (SH - top) / ROW;
    float want = std::max(0, std::min(sel - vis / 2, rows - vis));
    scroll = approach(scroll, want, 14, dt);
    hi = approach(hi, sel, 20, dt);
    int hy = top + (int)((hi - scroll) * ROW);
    uint16_t hc = armT > 0 ? col::CINNABAR : col::GOLD;
    roundPanel(4, hy, SW - 8, ROW - 2, 5, blend565(col::INK, hc, 0.12f), blend565(col::INK, hc, 0.6f));
    for (int i = (int)scroll; i < rows && i <= (int)scroll + vis; ++i) {
      int y = top + (int)((i - scroll) * ROW);
      bool on = i == sel;
      if (i == pomo::count()) {
        text("+ 新建", 14, y + 3, &F12, on ? col::GOLD : col::MUTE);
        continue;
      }
      const pomo::Preset& p = pomo::get(i);
      cv.fillCircle(14, y + 10, 3, on ? col::CINNABAR : col::INK3);
      text(p.name, 24, y + 3, &F12, on ? col::BONE : col::DIM);
      if (on && armT > 0) text("D 确认删除", SW - 12, y + 3, &F12, col::CINNABAR, AR);
      else {
        snprintf(b, sizeof b, "%d · %d", p.focus, p.shortBreak);
        textMono(b, SW - 12, y + 3, &F12, on ? col::GOLD : col::MUTE, 7, AR);
      }
    }
  }

  void drawRun() {
    pomo::Phase p = pomo::phase();
    uint16_t c = phaseColor(p);
    bool paused = pomo::paused();
    int cx = 66, cy = 68;
    ring(cx, cy, 50, 5, pomo::progress(), paused ? col::MUTE : c, t, !paused);
    char b[24];
    fmtMs(pomo::remainingMs(), b, sizeof b);
    bool blink = paused && ((millis() / 500) & 1);
    textMono(b, cx, cy - 16, &F24, blink ? col::MUTE : col::WHITE, 14, AC);
    const char* ph = p == pomo::FOCUS ? "专注" : p == pomo::SHORT ? "小憩" : "长休";
    text(paused ? "暂停" : ph, cx, cy + 17, &F12, paused ? col::DIM : c, AC);

    const pomo::Preset& pr = pomo::get(pomo::presetIdx());
    int x = 130;
    text(pr.name, x, 22, &F16, col::BONE);
    // rounds toward the long break
    int every = std::max(1, (int)pr.every);
    int doneIn = pr.every ? pomo::round() % every : pomo::round();
    int dots = pr.every ? every : std::min(8, pomo::round() + 1);
    for (int i = 0; i < dots; ++i) {
      bool on = i < doneIn || (pr.every && doneIn == 0 && pomo::round() > 0 && p == pomo::LONG);
      int dx = x + 4 + i * 11;
      if (on) cv.fillCircle(dx, 52, 3, col::CINNABAR);
      else cv.drawCircle(dx, 52, 3, col::LINE);
    }
    snprintf(b, sizeof b, "今日 %d′", pomo::todayMinutes());
    text(b, x, 66, &F12, col::DIM);
    if (armT > 0) text("再按 X 结束", x, 110, &F12, col::CINNABAR);
    else if (paused && p == pomo::FOCUS && pomo::progress() == 0) text("空格开始", x, 110, &F12, col::GOLD);
  }

  void drawStats() {
    statsT += 1 / 30.0f;
    text("专注统计", 6, 2, &F12, col::DIM);
    char b[24];
    snprintf(b, sizeof b, "%d", days[6].minutes);
    int w = textMono(b, 10, 18, &FBIG, col::WHITE, 19);
    text("分钟", 14 + w, 38, &F12, col::DIM);
    snprintf(b, sizeof b, "%d 个番茄", days[6].sessions);
    text(b, SW - 8, 22, &F12, col::GOLD, AR);
    if (streak > 0) {
      snprintf(b, sizeof b, "连续 %d 天", streak);
      text(b, SW - 8, 38, &F12, col::CINNABAR, AR);
    }
    int mx = 30;
    for (auto& d : days) mx = std::max(mx, d.minutes);
    struct tm tm;
    int wday = timeLocal(tm) ? tm.tm_wday : 0;
    static const char* wk[] = {"日", "一", "二", "三", "四", "五", "六"};
    for (int i = 0; i < 7; ++i) {
      int x = 22 + i * 30, base = 116;
      float e = easeOutCubic(clamp01((statsT - i * 0.05f) / 0.5f));
      int h = (int)(e * 48.0f * days[i].minutes / mx);
      uint16_t c = i == 6 ? col::CINNABAR : col::GOLD2;
      cv.fillRoundRect(x - 6, base - h, 12, std::max(1, h), 2, c);
      if (i == 6) glowDot(x, base - h, 8, col::CINNABAR, 0.3f);
      text(wk[(wday + i + 1) % 7], x, base + 3, &F12, i == 6 ? col::BONE : col::MUTE, AC);
    }
  }

  void drawEdit() {
    text(editIdx < pomo::count() ? "编辑" : "新建", 6, 2, &F12, col::DIM);
    static const char* names[] = {"名称", "专注", "短休", "长休", "长休间隔"};
    for (int i = 0; i < 5; ++i) {
      int y = 20 + i * 22;
      bool on = i == field;
      if (on) roundPanel(4, y, SW - 8, 20, 5, blend565(col::INK, col::GOLD, 0.1f), blend565(col::INK, col::GOLD, 0.5f));
      text(names[i], 12, y + 3, &F12, on ? col::BONE : col::DIM);
      char b[32];
      if (i == 0) {
        snprintf(b, sizeof b, "%s%s", work.name, typing && ((millis() / 450) & 1) ? "_" : "");
      } else {
        uint8_t v = i == 1 ? work.focus : i == 2 ? work.shortBreak : i == 3 ? work.longBreak : work.every;
        if (i == 4) snprintf(b, sizeof b, v ? "‹ 每 %d 轮 ›" : "‹ 不长休 ›", v);
        else snprintf(b, sizeof b, "‹ %d 分钟 ›", v);
      }
      text(b, SW - 12, y + 3, &F12, on ? col::GOLD : col::MUTE, AR);
    }
  }
};

}  // namespace

App* makePomo() { return new Pomo(); }

// Small focus badge for other screens and the home page: a ring with the time left.
void drawPomoBadge(int x, int y, bool withText) {
  pomo::Phase p = pomo::phase();
  if (p == pomo::IDLE) return;
  uint16_t c = pomo::paused() ? col::MUTE : phaseColor(p);
  cv.fillArc(x, y, 6, 4, 0, 360, col::INK3);
  cv.fillArc(x, y, 6, 4, -90, -90 + 360 * pomo::progress(), c);
  if (withText) {
    char b[12];
    fmtMs(pomo::remainingMs(), b, sizeof b);
    textMono(b, x + 10, y - 7, &F12, c, 7);
  }
}
