// Frame canvas, fonts, colour palette and drawing helpers shared by every screen.
#pragma once
#include <M5Cardputer.h>

static constexpr int SW = 240;
static constexpr int SH = 135;

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

namespace col {
constexpr uint16_t INK      = rgb(9, 9, 13);
constexpr uint16_t INK2     = rgb(18, 18, 25);
constexpr uint16_t INK3     = rgb(30, 30, 40);
constexpr uint16_t LINE     = rgb(52, 50, 60);
constexpr uint16_t MUTE     = rgb(92, 88, 84);
constexpr uint16_t DIM      = rgb(140, 133, 120);
constexpr uint16_t BONE     = rgb(238, 231, 216);
constexpr uint16_t WHITE    = rgb(255, 252, 244);
constexpr uint16_t GOLD     = rgb(214, 172, 96);
constexpr uint16_t GOLD2    = rgb(150, 112, 52);
constexpr uint16_t CINNABAR = rgb(214, 58, 44);
constexpr uint16_t EMBER    = rgb(255, 110, 64);
constexpr uint16_t JADE     = rgb(98, 176, 146);
constexpr uint16_t SKY      = rgb(110, 160, 220);
constexpr uint16_t PAPER    = rgb(234, 224, 200);
constexpr uint16_t PAPER2   = rgb(214, 200, 170);
constexpr uint16_t SUMI     = rgb(36, 30, 26);
}  // namespace col

extern M5Canvas cv;
extern lgfx::VLWfont F12, F16, F24, FBIG;

void gfxInit();
// Push the frame to the panel; handles the screen-change wipe.
void present();
void startWipe(bool fromRight = true);

// --- colour math (normal RGB565) ---
uint16_t blend565(uint16_t a, uint16_t b, float t);  // t=0 -> a, t=1 -> b
uint16_t scale565(uint16_t c, float k);
uint16_t hsv565(float h, float s, float v);

// --- direct framebuffer effects ---
void darkenRect(int x, int y, int w, int h, float k);           // multiply by k
void tintRect(int x, int y, int w, int h, uint16_t c, float a);  // blend toward c
void softShadow(float cx, float cy, float rx, float ry, float strength);
void glowDot(float cx, float cy, float r, uint16_t c, float strength);
void vgrad(int y0, int y1, uint16_t c0, uint16_t c1);
void noiseOverlay(uint8_t amount, uint32_t seed);

// --- text ---
enum Align { AL = 0, AC = 1, AR = 2 };
int textW(const char* s, const lgfx::IFont* f);
void text(const char* s, int x, int y, const lgfx::IFont* f, uint16_t c, Align a = AL);
// y is the top of the text box. Fixed-pitch digits stop numbers from jittering.
int textMono(const char* s, int x, int y, const lgfx::IFont* f, uint16_t c, int cell, Align a = AL);
// Vertical (top-to-bottom) text column; shown limits how many glyphs appear, fade the last one.
void textV(const char* s, int x, int y, const lgfx::IFont* f, uint16_t c, int pitch, float shown = 1e9f,
           uint16_t bg = col::INK);
int utf8Len(const char* s);
uint32_t utf8Next(const char*& p);
// Wrap CJK/latin text into width; returns number of lines drawn.
int textWrap(const char* s, int x, int y, int w, const lgfx::IFont* f, uint16_t c, int lineH, int maxLines = 99,
             int skipLines = 0);

// --- easing ---
float clamp01(float v);
float lerpf(float a, float b, float t);
float easeOutCubic(float t);
float easeInOutCubic(float t);
float easeOutBack(float t);
float easeOutElastic(float t);
float approach(float cur, float target, float rate, float dt);  // exponential smoothing
float frand(float a, float b);

// --- widgets ---
void statusBar(bool showClock = true, bool showRec = true);
void hintBar(const char* s);
void roundPanel(int x, int y, int w, int h, int r, uint16_t fill, uint16_t border);
void seal(int x, int y, int size, const char* chars, uint16_t c);  // red square stamp
void toast(const char* msg, uint16_t c = col::BONE);
void drawToast(float dt);
void drawIcon(int id, int cx, int cy, uint16_t c, float t);
