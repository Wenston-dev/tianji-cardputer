#include "gfx.h"
#include "system.h"
#include "rec.h"

M5Canvas cv(&M5.Display);
lgfx::VLWfont F12, F16, F24, FBIG;

extern const uint8_t FONT_F12[];
extern const uint8_t FONT_F16[];
extern const uint8_t FONT_F24[];
extern const uint8_t FONT_FBIG[];

static lgfx::PointerWrapper w12, w16, w24, wbig;

static bool wipeOn = false;
static bool wipeRight = true;
static uint32_t wipeT0 = 0;

void gfxInit() {
  M5.Display.setRotation(1);
  M5.Display.fillScreen(col::INK);
  cv.setColorDepth(16);
  cv.setPsram(false);
  cv.createSprite(SW, SH);
  cv.fillSprite(col::INK);
  w12.set(FONT_F12);
  w16.set(FONT_F16);
  w24.set(FONT_F24);
  wbig.set(FONT_FBIG);
  F12.loadFont(&w12);
  F16.loadFont(&w16);
  F24.loadFont(&w24);
  FBIG.loadFont(&wbig);
}

void startWipe(bool fromRight) {
  wipeOn = true;
  wipeRight = fromRight;
  wipeT0 = millis();
}

void present() {
  if (!wipeOn) {
    cv.pushSprite(0, 0);
    return;
  }
  float t = (millis() - wipeT0) / 260.0f;
  if (t >= 1.0f) {
    wipeOn = false;
    cv.pushSprite(0, 0);
    return;
  }
  int x = (int)(easeInOutCubic(t) * SW);
  int edge = wipeRight ? SW - x : x;
  if (wipeRight) M5.Display.setClipRect(edge, 0, SW - edge, SH);
  else M5.Display.setClipRect(0, 0, edge, SH);
  cv.pushSprite(0, 0);
  M5.Display.clearClipRect();
  // a thin gold seam rides the edge of the wipe
  int e = wipeRight ? edge - 1 : edge;
  if (e >= 0 && e < SW) M5.Display.drawFastVLine(e, 0, SH, col::GOLD);
  int e2 = wipeRight ? edge - 2 : edge + 1;
  if (e2 >= 0 && e2 < SW) M5.Display.drawFastVLine(e2, 0, SH, col::GOLD2);
}

// ---------------------------------------------------------------- colour math

static inline void unpack(uint16_t c, int& r, int& g, int& b) {
  r = (c >> 11) & 31;
  g = (c >> 5) & 63;
  b = c & 31;
}

uint16_t blend565(uint16_t a, uint16_t b, float t) {
  if (t <= 0) return a;
  if (t >= 1) return b;
  int ar, ag, ab, br, bg, bb;
  unpack(a, ar, ag, ab);
  unpack(b, br, bg, bb);
  int k = (int)(t * 256);
  int r = ar + (((br - ar) * k) >> 8);
  int g = ag + (((bg - ag) * k) >> 8);
  int bl = ab + (((bb - ab) * k) >> 8);
  return (r << 11) | (g << 5) | bl;
}

uint16_t scale565(uint16_t c, float k) {
  int r, g, b;
  unpack(c, r, g, b);
  r = std::min(31, (int)(r * k));
  g = std::min(63, (int)(g * k));
  b = std::min(31, (int)(b * k));
  return (r << 11) | (g << 5) | b;
}

uint16_t hsv565(float h, float s, float v) {
  h = fmodf(h, 1.0f);
  if (h < 0) h += 1;
  float r, g, b;
  int i = (int)(h * 6);
  float f = h * 6 - i, p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
  switch (i % 6) {
    case 0: r = v, g = t, b = p; break;
    case 1: r = q, g = v, b = p; break;
    case 2: r = p, g = v, b = t; break;
    case 3: r = p, g = q, b = v; break;
    case 4: r = t, g = p, b = v; break;
    default: r = v, g = p, b = q; break;
  }
  return rgb(r * 255, g * 255, b * 255);
}

// ---------------------------------------------------------------- framebuffer effects

static inline uint16_t* fb() { return (uint16_t*)cv.getBuffer(); }
static inline uint16_t rd(uint16_t v) { return __builtin_bswap16(v); }

void darkenRect(int x, int y, int w, int h, float k) {
  int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(SW, x + w), y1 = std::min(SH, y + h);
  int kk = (int)(k * 256);
  uint16_t* p = fb();
  for (int yy = y0; yy < y1; ++yy) {
    uint16_t* row = p + yy * SW;
    for (int xx = x0; xx < x1; ++xx) {
      uint16_t c = rd(row[xx]);
      int r = (((c >> 11) & 31) * kk) >> 8, g = (((c >> 5) & 63) * kk) >> 8, b = ((c & 31) * kk) >> 8;
      row[xx] = rd((r << 11) | (g << 5) | b);
    }
  }
}

void tintRect(int x, int y, int w, int h, uint16_t c, float a) {
  int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(SW, x + w), y1 = std::min(SH, y + h);
  uint16_t* p = fb();
  for (int yy = y0; yy < y1; ++yy) {
    uint16_t* row = p + yy * SW;
    for (int xx = x0; xx < x1; ++xx) row[xx] = rd(blend565(rd(row[xx]), c, a));
  }
}

void softShadow(float cx, float cy, float rx, float ry, float strength) {
  if (rx < 1 || ry < 1) return;
  int x0 = std::max(0, (int)(cx - rx)), x1 = std::min(SW - 1, (int)(cx + rx) + 1);
  int y0 = std::max(0, (int)(cy - ry)), y1 = std::min(SH - 1, (int)(cy + ry) + 1);
  uint16_t* p = fb();
  float irx = 1.0f / rx, iry = 1.0f / ry;
  for (int y = y0; y <= y1; ++y) {
    float dy = (y - cy) * iry;
    float rem = 1 - dy * dy;
    if (rem <= 0) continue;
    float span = sqrtf(rem) * rx;
    int xa = std::max(x0, (int)(cx - span)), xb = std::min(x1, (int)(cx + span) + 1);
    uint16_t* row = p + y * SW;
    for (int x = xa; x <= xb; ++x) {
      float dx = (x - cx) * irx;
      float d = dx * dx + dy * dy;
      if (d >= 1) continue;
      float a = 1 - d;
      int k = 256 - (int)(a * a * strength * 256);
      uint16_t c = rd(row[x]);
      int r = (((c >> 11) & 31) * k) >> 8, g = (((c >> 5) & 63) * k) >> 8, b = ((c & 31) * k) >> 8;
      row[x] = rd((r << 11) | (g << 5) | b);
    }
  }
}

void glowDot(float cx, float cy, float r, uint16_t c, float strength) {
  if (r < 0.5f) return;
  int x0 = std::max(0, (int)(cx - r)), x1 = std::min(SW - 1, (int)(cx + r) + 1);
  int y0 = std::max(0, (int)(cy - r)), y1 = std::min(SH - 1, (int)(cy + r) + 1);
  uint16_t* p = fb();
  float ir = 1.0f / r;
  int cr, cg, cb;
  unpack(c, cr, cg, cb);
  for (int y = y0; y <= y1; ++y) {
    float dy = (y - cy) * ir;
    float rem = 1 - dy * dy;
    if (rem <= 0) continue;
    float span = sqrtf(rem) * r;
    int xa = std::max(x0, (int)(cx - span)), xb = std::min(x1, (int)(cx + span) + 1);
    uint16_t* row = p + y * SW;
    for (int x = xa; x <= xb; ++x) {
      float dx = (x - cx) * ir;
      float d = dx * dx + dy * dy;
      if (d >= 1) continue;
      float a = (1 - d);
      int k = (int)(a * a * strength * 256);
      if (k <= 0) continue;
      int pr, pg, pb;
      unpack(rd(row[x]), pr, pg, pb);
      // additive light
      pr = std::min(31, pr + ((cr * k) >> 8));
      pg = std::min(63, pg + ((cg * k) >> 8));
      pb = std::min(31, pb + ((cb * k) >> 8));
      row[x] = rd((pr << 11) | (pg << 5) | pb);
    }
  }
}

void vgrad(int y0, int y1, uint16_t c0, uint16_t c1) {
  int n = std::max(1, y1 - y0);
  for (int y = y0; y < y1; ++y) cv.drawFastHLine(0, y, SW, blend565(c0, c1, (float)(y - y0) / n));
}

void noiseOverlay(uint8_t amount, uint32_t seed) {
  uint16_t* p = fb();
  uint32_t s = seed * 2654435761u + 1;
  for (int i = 0; i < amount * 40; ++i) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    int idx = s % (SW * SH);
    p[idx] = rd(scale565(rd(p[idx]), 1.18f));
  }
}

// ---------------------------------------------------------------- text

uint32_t utf8Next(const char*& p) {
  uint8_t c = (uint8_t)*p;
  if (!c) return 0;
  uint32_t cp;
  int n;
  if (c < 0x80) { cp = c; n = 0; }
  else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 1; }
  else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 2; }
  else { cp = c & 0x07; n = 3; }
  ++p;
  while (n-- && *p) cp = (cp << 6) | ((uint8_t)*p++ & 0x3F);
  return cp;
}

static int utf8Enc(uint32_t cp, char* out) {
  if (cp < 0x80) { out[0] = cp; out[1] = 0; return 1; }
  if (cp < 0x800) { out[0] = 0xC0 | (cp >> 6); out[1] = 0x80 | (cp & 0x3F); out[2] = 0; return 2; }
  out[0] = 0xE0 | (cp >> 12);
  out[1] = 0x80 | ((cp >> 6) & 0x3F);
  out[2] = 0x80 | (cp & 0x3F);
  out[3] = 0;
  return 3;
}

int utf8Len(const char* s) {
  int n = 0;
  while (utf8Next(s)) ++n;
  return n;
}

int textW(const char* s, const lgfx::IFont* f) {
  cv.setFont(f);
  return cv.textWidth(s);
}

void text(const char* s, int x, int y, const lgfx::IFont* f, uint16_t c, Align a) {
  cv.setFont(f);
  cv.setTextColor(c);
  cv.setTextDatum(a == AL ? top_left : a == AC ? top_center : top_right);
  cv.drawString(s, x, y);
}

int textMono(const char* s, int x, int y, const lgfx::IFont* f, uint16_t c, int cell, Align a) {
  cv.setFont(f);
  cv.setTextColor(c);
  cv.setTextDatum(top_center);
  // narrow separators get half a cell
  int total = 0;
  for (const char* p = s; *p; ++p) total += (*p == ':' || *p == '.' || *p == ',') ? cell / 2 + 1 : cell;
  int cx = a == AL ? x : a == AC ? x - total / 2 : x - total;
  char buf[2] = {0, 0};
  for (const char* p = s; *p; ++p) {
    int w = (*p == ':' || *p == '.' || *p == ',') ? cell / 2 + 1 : cell;
    buf[0] = *p;
    cv.drawString(buf, cx + w / 2, y);
    cx += w;
  }
  return total;
}

void textV(const char* s, int x, int y, const lgfx::IFont* f, uint16_t c, int pitch, float shown, uint16_t bg) {
  cv.setFont(f);
  cv.setTextDatum(top_center);
  char buf[5];
  int i = 0;
  const char* p = s;
  while (uint32_t cp = utf8Next(p)) {
    if (i >= shown) break;
    float a = clamp01(shown - i);
    cv.setTextColor(a >= 1 ? c : blend565(bg, c, a));
    utf8Enc(cp, buf);
    cv.drawString(buf, x, y + i * pitch);
    ++i;
  }
}

int textWrap(const char* s, int x, int y, int w, const lgfx::IFont* f, uint16_t c, int lineH, int maxLines,
             int skipLines) {
  cv.setFont(f);
  cv.setTextColor(c);
  cv.setTextDatum(top_left);
  char line[256];
  int len = 0, lw = 0, lines = 0;
  const char* p = s;
  auto flush = [&]() {
    line[len] = 0;
    if (lines >= skipLines && lines - skipLines < maxLines) cv.drawString(line, x, y + (lines - skipLines) * lineH);
    ++lines;
    len = 0;
    lw = 0;
  };
  char ch[5];
  while (true) {
    const char* q = p;
    uint32_t cp = utf8Next(p);
    if (!cp) break;
    if (cp == '\n') { flush(); continue; }
    int n = utf8Enc(cp, ch);
    int cw = cv.textWidth(ch);
    if (lw + cw > w && len > 0) {
      // keep closing punctuation on the line it belongs to
      bool punct = (cp == 0xFF0C || cp == 0x3002 || cp == 0xFF1B || cp == 0xFF01 || cp == 0xFF1F || cp == 0x3001);
      if (!punct) flush();
    }
    (void)q;
    if (len + n < (int)sizeof(line) - 1) {
      memcpy(line + len, ch, n);
      len += n;
      lw += cw;
    }
  }
  if (len) flush();
  return lines;
}

// ---------------------------------------------------------------- easing

float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
float lerpf(float a, float b, float t) { return a + (b - a) * t; }
float easeOutCubic(float t) {
  t = clamp01(t);
  float u = 1 - t;
  return 1 - u * u * u;
}
float easeInOutCubic(float t) {
  t = clamp01(t);
  return t < 0.5f ? 4 * t * t * t : 1 - powf(-2 * t + 2, 3) / 2;
}
float easeOutBack(float t) {
  t = clamp01(t);
  const float c1 = 1.70158f, c3 = c1 + 1;
  return 1 + c3 * powf(t - 1, 3) + c1 * powf(t - 1, 2);
}
float easeOutElastic(float t) {
  t = clamp01(t);
  if (t == 0 || t == 1) return t;
  return powf(2, -10 * t) * sinf((t * 10 - 0.75f) * (2 * PI / 3)) + 1;
}
float approach(float cur, float target, float rate, float dt) {
  return target + (cur - target) * expf(-rate * dt);
}
float frand(float a, float b) { return a + (b - a) * (esp_random() / 4294967296.0f); }

// ---------------------------------------------------------------- widgets

static void battery(int x, int y, int pct, bool charging) {
  uint16_t c = pct <= 15 ? col::CINNABAR : col::DIM;
  cv.drawRoundRect(x, y, 17, 8, 2, c);
  cv.fillRect(x + 17, y + 2, 2, 4, c);
  int w = std::max(1, pct * 13 / 100);
  cv.fillRect(x + 2, y + 2, w, 4, pct <= 15 ? col::CINNABAR : (charging ? col::JADE : col::BONE));
}

void statusBar(bool showClock, bool showRec) {
  char buf[48];
  if (showClock) {
    struct tm t;
    if (timeLocal(t)) {
      static const char* wk[] = {"日", "一", "二", "三", "四", "五", "六"};
      snprintf(buf, sizeof buf, "%d月%d日 周%s", t.tm_mon + 1, t.tm_mday, wk[t.tm_wday]);
      text(buf, 4, 0, &F12, col::DIM);
      snprintf(buf, sizeof buf, "%02d:%02d", t.tm_hour, t.tm_min);
      textMono(buf, SW - 28, 1, &F12, timeState() >= TIME_SET ? col::BONE : col::MUTE, 7, AR);
    } else {
      text("时间未设置", 4, 0, &F12, col::MUTE);
    }
  }
  battery(SW - 23, 3, batteryPct(), batteryCharging());
  if (wifiOn()) {
    // three short arcs
    int wx = SW - 32, wy = 10;
    for (int r = 2; r <= 6; r += 2) cv.drawArc(wx, wy, r, r, 225, 315, col::DIM);
    cv.drawPixel(wx, wy, col::DIM);
  }
  if (showRec && rec::state() != rec::IDLE) {
    uint32_t s = rec::elapsedMs() / 1000;
    snprintf(buf, sizeof buf, "%02lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
    int w = 44;
    int x = SW / 2 - w / 2 + 30;
    bool blink = (millis() / 500) & 1;
    cv.fillRoundRect(x, 1, w, 11, 5, rgb(60, 14, 12));
    if (rec::state() == rec::RECORDING && blink) cv.fillCircle(x + 6, 6, 2, col::CINNABAR);
    else cv.drawCircle(x + 6, 6, 2, col::CINNABAR);
    textMono(buf, x + 12, 0, &F12, col::EMBER, 6);
  }
}

void hintBar(const char* s) {
  text(s, SW / 2, SH - 13, &F12, col::MUTE, AC);
}

void roundPanel(int x, int y, int w, int h, int r, uint16_t fill, uint16_t border) {
  cv.fillRoundRect(x, y, w, h, r, fill);
  if (border != fill) cv.drawRoundRect(x, y, w, h, r, border);
}

void seal(int x, int y, int size, const char* chars, uint16_t c) {
  cv.fillRoundRect(x, y, size, size, 2, c);
  cv.drawRoundRect(x + 2, y + 2, size - 4, size - 4, 1, col::PAPER);
  // up to two glyphs, stacked vertically like a carved seal
  const lgfx::IFont* f = size >= 30 ? (const lgfx::IFont*)&F12 : (const lgfx::IFont*)&F12;
  int n = utf8Len(chars);
  if (n <= 2) textV(chars, x + size / 2, y + (size - n * 12) / 2 - 2, f, col::PAPER, 12, 9, c);
  else {
    // 2x2 grid, read right column first
    const char* p = chars;
    char g[4][5];
    for (int i = 0; i < 4; ++i) {
      uint32_t cp = utf8Next(p);
      utf8Enc(cp, g[i]);
    }
    int h = size / 2;
    text(g[0], x + size - h / 2 - 1, y + 2, f, col::PAPER, AC);
    text(g[1], x + size - h / 2 - 1, y + h, f, col::PAPER, AC);
    text(g[2], x + h / 2 + 1, y + 2, f, col::PAPER, AC);
    text(g[3], x + h / 2 + 1, y + h, f, col::PAPER, AC);
  }
}

static char toastMsg[64];
static uint16_t toastCol;
static float toastT = -1;

void toast(const char* msg, uint16_t c) {
  strlcpy(toastMsg, msg, sizeof toastMsg);
  toastCol = c;
  toastT = 0;
}

void drawToast(float dt) {
  if (toastT < 0) return;
  toastT += dt;
  const float life = 2.2f;
  if (toastT > life) {
    toastT = -1;
    return;
  }
  float in = easeOutBack(toastT / 0.35f);
  float out = clamp01((life - toastT) / 0.3f);
  int w = textW(toastMsg, &F12) + 18;
  int y = (int)lerpf(-16, 16, in * out);
  int x = SW / 2 - w / 2;
  darkenRect(x, y, w, 16, 0.25f);
  cv.drawRoundRect(x, y, w, 16, 7, blend565(col::INK, toastCol, 0.6f));
  text(toastMsg, SW / 2, y + 2, &F12, toastCol, AC);
}

// Procedural launcher icons, ~18px.
void drawIcon(int id, int cx, int cy, uint16_t c, float t) {
  switch (id) {
    case 0: {  // liuren: six stars on a ring
      for (int i = 0; i < 6; ++i) {
        float a = -PI / 2 + i * PI / 3 + t * 0.6f;
        cv.fillCircle(cx + cosf(a) * 7, cy + sinf(a) * 7, i == 0 ? 2 : 1, i == 0 ? col::GOLD : c);
      }
      cv.drawCircle(cx, cy, 3, c);
    } break;
    case 1: {  // qian: tube with sticks
      cv.drawLine(cx - 4, cy - 8, cx - 2, cy - 1, c);
      cv.drawLine(cx, cy - 9, cx, cy - 1, col::CINNABAR);
      cv.drawLine(cx + 4, cy - 8, cx + 2, cy - 1, c);
      cv.fillRoundRect(cx - 6, cy - 2, 12, 11, 2, c);
      cv.drawFastHLine(cx - 6, cy + 3, 12, col::INK);
    } break;
    case 2: {  // jiao: two crescents
      cv.fillArc(cx - 3, cy + 1, 7, 4, 200, 340, col::CINNABAR);
      cv.fillArc(cx + 4, cy - 1, 7, 4, 20, 160, c);
    } break;
    case 3: {  // dice
      cv.fillRoundRect(cx - 7, cy - 7, 14, 14, 3, c);
      cv.fillCircle(cx, cy, 2, col::CINNABAR);
      cv.fillCircle(cx - 4, cy - 4, 1, col::INK);
      cv.fillCircle(cx + 4, cy + 4, 1, col::INK);
      cv.fillCircle(cx + 4, cy - 4, 1, col::INK);
      cv.fillCircle(cx - 4, cy + 4, 1, col::INK);
    } break;
    case 4: {  // coin with square hole
      cv.fillCircle(cx, cy, 8, c);
      cv.drawCircle(cx, cy, 6, col::INK);
      cv.fillRect(cx - 2, cy - 2, 5, 5, col::INK);
    } break;
    case 5: {  // record
      cv.drawCircle(cx, cy, 8, c);
      float p = 0.5f + 0.5f * sinf(t * 4);
      cv.fillCircle(cx, cy, 4 + (int)(p * 1.5f), col::CINNABAR);
    } break;
    case 6: {  // library: waveform bars
      static const int8_t hgt[] = {3, 6, 9, 5, 7, 3};
      for (int i = 0; i < 6; ++i) {
        int h = hgt[i] + (int)(2 * sinf(t * 3 + i));
        cv.fillRoundRect(cx - 8 + i * 3, cy - h, 2, h * 2, 1, i == 2 ? col::GOLD : c);
      }
    } break;
    case 8: {  // pomodoro: a tomato
      cv.fillCircle(cx, cy + 1, 7, col::CINNABAR);
      cv.fillCircle(cx - 2, cy - 1, 2, blend565(col::CINNABAR, col::WHITE, 0.4f));
      cv.fillTriangle(cx - 4, cy - 6, cx + 4, cy - 6, cx, cy - 3, col::JADE);
      cv.drawLine(cx, cy - 6, cx + 1, cy - 9, col::JADE);
    } break;
    case 9: {  // page turner: a little e-reader with a turning corner
      cv.drawRoundRect(cx - 6, cy - 8, 12, 16, 2, c);
      for (int i = 0; i < 3; ++i) cv.drawFastHLine(cx - 3, cy - 4 + i * 3, 6, c);
      cv.fillTriangle(cx + 1, cy + 7, cx + 5, cy + 7, cx + 5, cy + 3, col::GOLD);
    } break;
    case 7: {  // settings: sliders
      for (int i = 0; i < 3; ++i) {
        int y = cy - 5 + i * 5;
        cv.drawFastHLine(cx - 7, y, 15, c);
        int kx = cx - 4 + ((i * 5 + 3) % 9);
        cv.fillCircle(kx, y, 2, i == 1 ? col::GOLD : c);
      }
    } break;
  }
}
