// 小六壬: count lunar month, day and double-hour around six palaces (or three spoken numbers).
#include "app.h"
#include "lunar.h"
#include "sfx.h"
#include "system.h"

namespace {

struct Palace {
  const char* name;
  const char* luck;
  const char* tag;
  const char* brief;
  const char* verse[8];
  uint16_t color;
};

const Palace kP[6] = {
    {"大安", "吉", "安稳", "事事安康，宜守不宜动。身未动时，静中有福。",
     {"大安事事昌", "求财在坤方", "失物去不远", "宅舍保安康", "行人身未动", "病者主无妨", "将军回田野", "仔细更推详"},
     col::JADE},
    {"留连", "平", "迟滞", "事多拖延，宜缓图之。所谋未明，耐心等待。",
     {"留连事难成", "求谋日未明", "官事只宜缓", "去者未回程", "失物南方见", "急讨方称心", "更须防口舌", "人口且平平"},
     col::SKY},
    {"速喜", "吉", "喜讯", "喜事临门，宜速行动。所求顺遂，消息将至。",
     {"速喜喜来临", "求财向南行", "失物申未午", "逢人路上寻", "官事有福德", "病者无祸侵", "田宅六畜吉", "行人有信音"},
     col::GOLD},
    {"赤口", "凶", "口舌", "谨防口舌，慎言慎行。遇事冷静，勿与人争。",
     {"赤口主口舌", "官非切要防", "失物速速讨", "行人有惊慌", "六畜多作怪", "病者出西方", "更须防咀咒", "诚恐染瘟皇"},
     col::CINNABAR},
    {"小吉", "吉", "和合", "小有所成，贵人相助。凡事和合，宜于交往。",
     {"小吉最吉昌", "路上好商量", "阴人来报喜", "失物在坤方", "行人即便至", "交关甚是强", "凡事皆和合", "病者叩穷苍"},
     col::GOLD},
    {"空亡", "凶", "落空", "所求难成，宜静待时。暂且放下，另寻他途。",
     {"空亡事不祥", "阴人多乖张", "求财无利益", "行人有灾殃", "失物寻不见", "官事有刑伤", "病人逢暗鬼", "解禳保安康"},
     col::MUTE},
};

constexpr float CX = 70, CY = 71, R = 40, RB = 56;

enum St { IDLE, RUN, DONE, VERSE };

class Liuren : public App {
 public:
  void enter() override {
    st = IDLE;
    mode = 0;
    nIn = 0;
    numBuf[0] = 0;
    pos = 0;
    markA = angleOf(0);
    t = 0;
    loadToday();
  }

  bool key(const KeyEvent& e) override {
    if (st == VERSE) {
      if (e.key == K_ESC || e.key == K_BACK || e.key == K_ENTER) st = DONE;
      return true;
    }
    if (st == RUN) return e.key != K_ESC;
    if (e.key == K_TAB) {
      mode ^= 1;
      st = IDLE;
      nIn = 0;
      numBuf[0] = 0;
      sfxTick(1, 0.5f);
      return true;
    }
    if (mode == 1 && st == IDLE) {
      if (e.key >= '0' && e.key <= '9' && nIn < 3) {
        size_t l = strlen(numBuf);
        if (l < 3) {
          numBuf[l] = e.key;
          numBuf[l + 1] = 0;
          sfxTick(1.3f, 0.4f);
        }
        return true;
      }
      if ((e.key == ' ' || e.key == K_ENTER) && numBuf[0]) {
        nums[nIn++] = std::max(1, atoi(numBuf));
        numBuf[0] = 0;
        sfxTick(0.9f, 0.5f);
        if (nIn == 3) begin(nums[0], nums[1], nums[2]);
        return true;
      }
      if (e.key == K_BACK && (numBuf[0] || nIn)) {
        size_t l = strlen(numBuf);
        if (l) numBuf[l - 1] = 0;
        else --nIn;
        return true;
      }
    }
    if (st == DONE && e.key == K_ENTER) {
      st = VERSE;
      verseT = 0;
      sfxWhoosh(0.4f);
      return true;
    }
    if ((e.key == ' ' || e.key == K_ENTER) && mode == 0) {
      loadToday();
      if (haveDate) begin(lm, ld, sc + 1);
      else toast("请先设置日期时间", col::CINNABAR);
      return true;
    }
    if (e.key == ' ' && mode == 1 && st == DONE) {
      st = IDLE;
      nIn = 0;
      return true;
    }
    return false;
  }

  void frame(float dt) override {
    t += dt;
    if (st == RUN) step(dt);
    if (st == DONE) resultT += dt;
    if (st == VERSE) {
      verseT += dt;
      drawVerse();
      return;
    }
    vgrad(0, SH, col::INK, rgb(14, 12, 20));
    drawWheel();
    drawPanel();
  }

 private:
  St st = IDLE;
  int mode = 0;
  float t = 0, verseT = 0;
  bool haveDate = false;
  int lm = 1, ld = 1, sc = 0;
  char lunarStr[32] = "";
  int nums[3] = {1, 1, 1}, nIn = 0;
  char numBuf[4] = "";

  // animation state
  int counts[3] = {1, 1, 1};
  int phase = 0, k = 0;  // phase 0..2, hop index within phase
  int pos = 0;           // palace index
  int startPos = 0;
  float hopT = 0, hopDur = 0.15f, pauseT = 0;
  float markA = 0, fromA = 0, toA = 0;
  float trail[10];
  float resultT = 0;
  int result = -1;

  static float angleOf(int i) { return -PI / 2 + i * PI / 3; }
  // Full laps land where they started, so big numbers run one lap plus the remainder.
  static int hopsFor(int count) { return count <= 13 ? count - 1 : 6 + (count - 1) % 6; }

  void loadToday() {
    struct tm tm;
    haveDate = false;
    if (!timeLocal(tm)) return;
    LunarDate L;
    if (!toLunar(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, L)) return;
    haveDate = true;
    lm = L.month;
    ld = L.day;
    sc = shichenIndex(tm.tm_hour);
    char dn[8];
    lunarDayName(ld, dn);
    snprintf(lunarStr, sizeof lunarStr, "%s%s%s", L.leap ? "闰" : "", lunarMonthName(lm), dn);
  }

  void begin(int a, int b, int c) {
    counts[0] = a;
    counts[1] = b;
    counts[2] = c;
    phase = 0;
    k = 0;
    pos = 0;
    markA = angleOf(0);
    for (auto& v : trail) v = markA;
    hopT = 0;
    pauseT = 0.35f;
    result = -1;
    st = RUN;
    sfxTick(0.7f, 0.7f);
  }

  void step(float dt) {
    if (pauseT > 0) {
      pauseT -= dt;
      return;
    }
    int total = hopsFor(counts[phase]);
    if (k >= total) {
      // phase complete
      ++phase;
      k = 0;
      if (phase >= 3) {
        result = pos;
        resultT = 0;
        st = DONE;
        sfxChime(strcmp(kP[result].luck, "吉") == 0 ? 1.0f : 0.75f, 0.8f);
        return;
      }
      pauseT = 0.45f;
      sfxKnock(1.0f + phase * 0.15f, 0.5f);
      return;
    }
    if (hopT == 0) {
      fromA = angleOf(pos);
      toA = fromA + PI / 3;
      // long counts accelerate so a 30-day month still takes about a second
      hopDur = std::max(0.035f, 0.16f * powf(0.9f, (float)k));
      if (total > 18) hopDur = std::max(0.028f, hopDur * 0.7f);
    }
    hopT += dt;
    float u = clamp01(hopT / hopDur);
    markA = lerpf(fromA, toA, easeInOutCubic(u));
    if (u >= 1) {
      hopT = 0;
      pos = (pos + 1) % 6;
      ++k;
      sfxTick(1.0f + 0.04f * (k % 12), 0.35f);
    }
  }

  void drawWheel() {
    // outer ring of the twelve branches, the current double-hour lit
    for (int i = 0; i < 12; ++i) {
      float a = -PI / 2 + i * PI / 6;
      int x = CX + cosf(a) * RB, y = CY + sinf(a) * RB;
      bool on = mode == 0 && haveDate && i == sc;
      if (on) glowDot(x, y, 10, col::GOLD, 0.35f);
      text(shichenName(i), x, y - 7, &F12, on ? col::GOLD : col::LINE, AC);
    }
    // slow dotted orbit
    for (int i = 0; i < 48; ++i) {
      float a = i * PI / 24 + t * 0.08f;
      cv.drawPixel(CX + cosf(a) * (R + 9), CY + sinf(a) * (R + 9), (i % 4) ? col::INK3 : col::LINE);
    }
    cv.drawCircle(CX, CY, R, col::INK3);
    cv.drawCircle(CX, CY, R - 13, col::INK2);

    // hexagram lines between palaces
    for (int i = 0; i < 6; ++i) {
      float a0 = angleOf(i), a1 = angleOf(i + 1);
      cv.drawLine(CX + cosf(a0) * R, CY + sinf(a0) * R, CX + cosf(a1) * R, CY + sinf(a1) * R, col::INK3);
    }

    // trail of the counting marker
    if (st == RUN) {
      for (int i = 9; i > 0; --i) trail[i] = trail[i - 1];
      trail[0] = markA;
      for (int i = 9; i >= 0; --i) {
        float a = trail[i];
        glowDot(CX + cosf(a) * R, CY + sinf(a) * R, 6 - i * 0.4f, col::GOLD, 0.25f * (1 - i / 10.0f));
      }
    }

    if (st == RUN) glowDot(CX + cosf(markA) * R, CY + sinf(markA) * R, 22, col::GOLD, 0.45f);
    for (int i = 0; i < 6; ++i) {
      float a = angleOf(i);
      int x = CX + cosf(a) * R, y = CY + sinf(a) * R;
      bool hit = (st == DONE && i == result);
      bool here = (st == RUN && i == pos);
      uint16_t c = hit ? kP[i].color : here ? col::WHITE : col::DIM;
      if (hit) {
        float p = 0.6f + 0.4f * sinf(t * 4);
        glowDot(x, y, 18, kP[i].color, 0.35f * p);
      }
      cv.fillRoundRect(x - 17, y - 8, 34, 16, 8, col::INK);
      cv.drawRoundRect(x - 17, y - 8, 34, 16, 8, hit ? kP[i].color : here ? col::GOLD : col::INK3);
      text(kP[i].name, x, y - 7, &F12, c, AC);
    }
    if (st == RUN) {
      // the travelling spark sits on the ring between pills
      float a = markA;
      glowDot(CX + cosf(a) * (R - 13), CY + sinf(a) * (R - 13), 5, col::WHITE, 0.7f);
    }

    // centre
    if (st == RUN) {
      static const char* ph[] = {"月", "日", "时"};
      int ph_ = std::min(phase, 2);
      text(ph[ph_], CX, CY - 18, &F24, col::GOLD, AC);
      char b[8];
      int tot = std::max(1, hopsFor(counts[ph_]));
      snprintf(b, sizeof b, "%d", 1 + (counts[ph_] - 1) * std::min(k, tot) / tot);
      textMono(b, CX, CY + 6, &F16, col::BONE, 9, AC);
    } else if (st == DONE) {
      float e = easeOutBack(resultT / 0.6f);
      uint16_t c = blend565(col::INK, kP[result].color, clamp01(resultT * 2));
      int dy = (int)((1 - e) * 10);
      text(kP[result].luck, CX, CY - 13 + dy, &F24, c, AC);
    } else {
      text("掐指", CX, CY - 8, &F16, col::LINE, AC);
    }
  }

  void drawPanel() {
    int x = 142;
    text(mode == 0 ? "时辰起课" : "报数起课", x, 4, &F16, col::GOLD);
    cv.drawFastHLine(x, 24, SW - x - 6, col::INK3);
    char b[48];
    if (st != DONE) {
      if (mode == 0) {
        if (haveDate) {
          text("农历", x, 30, &F12, col::MUTE);
          text(lunarStr, x + 28, 30, &F12, col::BONE);
          snprintf(b, sizeof b, "%s时", shichenName(sc));
          text("时辰", x, 46, &F12, col::MUTE);
          text(b, x + 28, 46, &F12, col::BONE);
        } else {
          text("尚未设置时间", x, 32, &F12, col::CINNABAR);
        }
      } else {
        for (int i = 0; i < 3; ++i) {
          int bx = x + i * 31, by = 50;
          bool active = i == nIn && st == IDLE;
          cv.drawRoundRect(bx, by, 27, 22, 4, active ? col::GOLD : col::INK3);
          if (i < nIn) snprintf(b, sizeof b, "%d", nums[i]);
          else if (active) snprintf(b, sizeof b, "%s%s", numBuf, ((millis() / 400) & 1) ? "_" : "");
          else b[0] = 0;
          text(b, bx + 13, by + 4, &F12, col::BONE, AC);
        }
      }
      if (st == IDLE) {
        float p = 0.5f + 0.5f * sinf(t * 3);
        if (mode == 0) text("空格", x, 100, &F12, blend565(col::GOLD2, col::GOLD, p));
        text("Tab", SW - 6, SH - 14, &F12, col::LINE, AR);
      }
      return;
    }
    const Palace& P = kP[result];
    float e = easeOutCubic(resultT / 0.5f);
    int dx = (int)((1 - e) * 16);
    uint16_t c = blend565(col::INK, P.color, e);
    text(P.name, x + dx, 26, &F24, c);
    snprintf(b, sizeof b, "%s · %s", P.luck, P.tag);
    int w = textW(b, &F12) + 14;
    cv.drawRoundRect(x + dx, 56, w, 16, 8, c);
    text(b, x + 7 + dx, 57, &F12, c);
    textWrap(P.brief, x + dx, 76, SW - x - 4, &F12, blend565(col::INK, col::DIM, e), 14, 3);
    text("诗诀 ⏎", SW - 4, SH - 14, &F12, col::LINE, AR);
  }

  void drawVerse() {
    cv.fillSprite(col::INK);
    const Palace& P = kP[result];
    // faint frame
    cv.drawRect(4, 4, SW - 8, SH - 8, col::INK3);
    cv.drawRect(6, 6, SW - 12, SH - 12, col::INK2);
    // title column on the right
    float e = clamp01(verseT / 0.5f);
    textV(P.name, SW - 24, 14, &F24, blend565(col::INK, P.color, e), 27);
    seal(SW - 33, 72, 18, P.luck, P.color);
    // eight verse columns, right to left, revealed like ink soaking into paper
    float shown = verseT * 14;
    for (int c = 0; c < 8; ++c) {
      int x = SW - 52 - c * 22;
      float s = shown - c * 5;
      if (s <= 0) break;
      textV(P.verse[c], x, 18, &F16, col::BONE, 19, s, col::INK);
    }
  }
};

}  // namespace

App* makeLiuren() { return new Liuren(); }
