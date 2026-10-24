// Home: the memento-mori life countdown, told mostly through motion. One number (days left),
// a draining line for today, a breathing grid of years and a life bar that sheds embers each second.
#include "app.h"
#include "rec.h"
#include "sfx.h"
#include "system.h"
#include "pomo.h"

namespace {

struct Mote {
  float x, y, vy, a;
};

struct Ember {
  float x, y, vx, vy, life;
};

class Home : public App {
 public:
  void enter() override {
    dockT = 0;
    dockOpen = false;
    for (auto& m : motes) m = {frand(0, SW), frand(0, SH), frand(-3, -0.8f), frand(0.15f, 0.5f)};
    for (auto& e : embers) e.life = 0;
  }
  bool drawsStatus() override { return true; }

  bool key(const KeyEvent& e) override {
    if (e.key == K_ESC || e.key == K_BACK) {
      dockOpen = false;
      return true;
    }
    if (e.key == K_ENTER || e.key == K_TAB || e.key == K_UP || e.key == K_DOWN || e.key == K_LEFT ||
        e.key == K_RIGHT || e.key == ' ') {
      lastKey = millis();
      if (!dockOpen) {
        dockOpen = true;
        sfxTick(0.8f, 0.5f);
        return true;
      }
      if (e.key == K_LEFT || e.key == K_UP) sel = (sel + kLaunchN - 1) % kLaunchN, sfxTick(1.2f, 0.4f);
      else if (e.key == K_RIGHT || e.key == K_DOWN || e.key == K_TAB) sel = (sel + 1) % kLaunchN, sfxTick(1.2f, 0.4f);
      else launch(sel);
      return true;
    }
    if (e.key == 'u') {
      openUsbDrive(false);
      return true;
    }
    if (e.key >= '1' && e.key <= '9') {
      launch(e.key - '1');
      return true;
    }
    for (int i = 0; i < kLaunchN; ++i)
      if (e.key == (uint8_t)kLaunch[i].key) {
        launch(i);
        return true;
      }
    return false;
  }

  void frame(float dt) override {
    t += dt;
    if (dockOpen && millis() - lastKey > 7000) dockOpen = false;
    dockT = approach(dockT, dockOpen ? 1 : 0, 14, dt);
    selX = approach(selX, sel, 18, dt);

    drawBackdrop(dt);
    drawTop();
    if (birthdaySet() && timeState() != TIME_NONE) drawCountdown(dt);
    else drawOnboarding();
    if (dockT > 0.01f) drawDock();
  }

 private:
  float t = 0, dockT = 0, selX = 0;
  bool dockOpen = false;
  int sel = 0;
  uint32_t lastKey = 0;
  long lastSec = 0;
  Mote motes[22];
  Ember embers[24];

  void launch(int i) {
    sel = i;
    dockOpen = false;
    sfxKnock(1.4f, 0.5f);
    go(kLaunch[i].id);
  }

  void drawBackdrop(float dt) {
    vgrad(0, SH, col::INK, rgb(16, 14, 20));
    for (auto& m : motes) {
      m.y += m.vy * dt;
      m.x += sinf(t * 0.3f + m.a * 20) * 0.6f * dt;
      if (m.y < -2) m = {frand(0, SW), (float)SH + 2, frand(-3, -0.8f), frand(0.15f, 0.5f)};
      float tw = 0.6f + 0.4f * sinf(t * 1.7f + m.a * 31);
      cv.drawPixel((int)m.x, (int)m.y, blend565(col::INK, col::GOLD2, m.a * tw));
    }
  }

  // Just the clock and battery; everything else on this screen is the countdown.
  void drawTop() {
    statusBar(false);
    struct tm tm;
    if (timeLocal(tm)) {
      char b[8];
      snprintf(b, sizeof b, "%02d:%02d", tm.tm_hour, tm.tm_min);
      textMono(b, 8, 1, &F12, col::MUTE, 7);
    }
  }

  void drawOnboarding() {
    float p = 0.5f + 0.5f * sinf(t * 3);
    text("以终为始", 12, 40, &F24, col::BONE);
    roundPanel(12, 82, 84, 18, 9, blend565(col::INK, col::GOLD2, 0.2f + 0.2f * p), col::GOLD2);
    text("S 设置生日", 54, 84, &F12, col::GOLD, AC);
    drawYearGrid(-1, 0, 0);
  }

  void drawCountdown(float dt) {
    double now = nowSec();
    double tz = S.tzMin * 60.0;
    double birth = daysFromCivil(S.birthY, S.birthM, S.birthD) * 86400.0 - tz;
    double death = daysFromCivil(S.birthY + S.lifespan, S.birthM, S.birthD) * 86400.0 - tz;
    double remain = death - now;
    double lived = now - birth;
    bool over = remain < 0;
    long days = (long)floor(fabs(remain) / 86400.0);
    float sub = (float)(now - floor(now));
    float beat = expf(-sub * 5.0f);  // a soft pulse at every second
    long sec = (long)now;
    bool newSec = sec != lastSec;
    lastSec = sec;

    // the number
    char buf[24], num[16];
    snprintf(num, sizeof num, "%ld", days);
    int n = strlen(num), j = 0;
    for (int i = 0; i < n; ++i) {
      buf[j++] = num[i];
      if ((n - i - 1) % 3 == 0 && i != n - 1) buf[j++] = ',';
    }
    buf[j] = 0;
    int nx = 10, ny = 42;
    int w = textMono(buf, nx, ny, &FBIG, over ? col::JADE : col::WHITE, 19);
    text("天", nx + w + 4, ny + 19, &F12, col::MUTE);

    // today: a line the width of the number that drains toward midnight
    double sod = fmod(now + tz, 86400.0);
    float leftToday = (float)(1.0 - sod / 86400.0);
    int ly = ny + 46, lw = w;
    cv.drawFastHLine(nx, ly, lw, col::INK3);
    int fw = (int)(lw * leftToday);
    for (int i = 0; i < fw; ++i) cv.drawPixel(nx + i, ly, blend565(col::GOLD2, col::BONE, (float)i / lw));
    glowDot(nx + fw, ly, 4 + beat * 3, col::BONE, 0.25f + 0.45f * beat);

    drawFocus(nx, ly + 12);
    float age = (float)(lived / (365.2425 * 86400.0));
    drawYearGrid(age, beat, 1);
    drawLifeBar((float)std::min(1.0, std::max(0.0, lived / (death - birth))), beat, newSec, dt);
  }

  // While a pomodoro runs, a slim tomato bar sits under the day line: it fills as the block passes.
  void drawFocus(int x, int y) {
    if (pomo::phase() == pomo::IDLE) return;
    bool focus = pomo::phase() == pomo::FOCUS;
    uint16_t c = pomo::paused() ? col::MUTE : (focus ? col::CINNABAR : col::JADE);
    drawPomoBadge(x + 6, y + 8, false);
    char b[12];
    uint32_t s = (pomo::remainingMs() + 999) / 1000;
    snprintf(b, sizeof b, "%02lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
    textMono(b, x + 18, y + 1, &F12, c, 7);
    // rounds done in this run as small seeds
    int r = pomo::round();
    for (int i = 0; i < std::min(r, 8); ++i) cv.fillCircle(x + 62 + i * 6, y + 8, 2, col::CINNABAR);
    if (focus && !pomo::paused()) {
      float br = 0.5f + 0.5f * sinf(t * 2 * PI / 10);
      glowDot(x + 6, y + 8, 9, c, 0.15f + 0.2f * br);
    }
  }

  // One cell per year of the expected life. Lived years are solid, the current one fills up with
  // the year's progress and breathes with the seconds.
  void drawYearGrid(float age, float beat, int live) {
    int L = S.lifespan;
    int cols = 10, rows = (L + cols - 1) / cols;
    int pitch = rows > 11 ? 6 : 8;
    int cell = pitch - 2;
    int gw = cols * pitch - 2, gh = rows * pitch - 2;
    int gx = SW - 12 - gw, gy = 20 + std::max(0, (96 - gh) / 2);
    int cur = live ? (int)age : -1;
    float frac = live ? age - cur : 0;
    for (int i = 0; i < L; ++i) {
      int x = gx + (i % cols) * pitch, y = gy + (i / cols) * pitch;
      if (i < cur) {
        cv.fillRect(x, y, cell, cell, col::MUTE);
      } else if (i == cur) {
        glowDot(x + cell / 2.0f, y + cell / 2.0f, 7 + beat * 3, col::GOLD, 0.2f + 0.3f * beat);
        cv.drawRect(x, y, cell, cell, col::GOLD);
        int fh = std::max(1, (int)(cell * frac));
        cv.fillRect(x, y + cell - fh, cell, fh, col::GOLD);
      } else {
        cv.drawRect(x, y, cell, cell, col::INK3);
      }
    }
  }

  // Whole-life bar along the bottom; each second an ember lifts off its burning end.
  void drawLifeBar(float frac, float beat, bool newSec, float dt) {
    int bx = 10, by = SH - 9, bw = SW - 20;
    cv.drawFastHLine(bx, by, bw, col::INK3);
    int fw = (int)(bw * frac);
    for (int i = 0; i < fw; ++i) cv.drawFastVLine(bx + i, by - 1, 2, blend565(col::GOLD2, col::GOLD, (float)i / bw));
    float hx = bx + fw;
    glowDot(hx, by, 6 + beat * 4, col::EMBER, 0.25f + 0.4f * beat);
    if (newSec) {
      for (auto& e : embers)
        if (e.life <= 0) {
          e = {hx, (float)by, frand(-4, 4), frand(-14, -8), 1.6f};
          break;
        }
    }
    for (auto& e : embers) {
      if (e.life <= 0) continue;
      e.life -= dt;
      e.x += e.vx * dt + sinf(t * 3 + e.y * 0.2f) * 0.15f;
      e.y += e.vy * dt;
      e.vy *= 0.985f;
      float a = clamp01(e.life / 1.6f);
      glowDot(e.x, e.y, 2 + a * 1.5f, col::EMBER, 0.5f * a);
    }
  }

  void drawDock() {
    float e = easeOutCubic(dockT);
    int h = 46;
    int y = SH - (int)(h * e);
    darkenRect(0, 0, SW, y, 1.0f - 0.55f * e);
    cv.fillRect(0, y, SW, SH - y, col::INK);
    cv.drawFastHLine(0, y, SW, blend565(col::INK, col::GOLD2, e));
    float pitch = (float)SW / kLaunchN;
    float sx = selX * pitch + pitch / 2.0f;
    glowDot(sx, y + 17, 16, col::GOLD, 0.18f * e);
    for (int i = 0; i < kLaunchN; ++i) {
      int cx = (int)(i * pitch + pitch / 2), cy = y + 17;
      bool on = i == sel;
      drawIcon(kLaunch[i].icon, cx, cy, on ? col::WHITE : col::DIM, t);
      char k[2] = {(char)toupper(kLaunch[i].key), 0};
      text(k, cx, y + 30, &F12, on ? col::GOLD : col::MUTE, AC);
    }
    cv.fillRoundRect((int)sx - 6, y + 44, 12, 2, 1, col::GOLD);
    const char* nm = kLaunch[sel].name;
    int w = textW(nm, &F12) + 14;
    int tx = std::max(2, std::min(SW - w - 2, (int)sx - w / 2));
    int ty = y - 18;
    darkenRect(tx, ty, w, 15, 0.3f);
    cv.drawRoundRect(tx, ty, w, 15, 6, col::GOLD2);
    text(nm, tx + w / 2, ty + 1, &F12, col::BONE, AC);
  }
};

}  // namespace

App* makeHome() { return new Home(); }
