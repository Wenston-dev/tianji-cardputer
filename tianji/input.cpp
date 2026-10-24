#include "input.h"
#include <M5Cardputer.h>

static bool textMode = false;
static KeyEvent q[24];
static uint8_t qh = 0, qt = 0;

struct Held {
  uint8_t pos;  // row*14+col
  uint32_t t0, tRep;
};
static Held held[8];
static uint8_t heldN = 0;
static float shake = 0;
static bool imuOk = false;
static float accBase = 0;  // resting magnitude, whatever units the IMU reports
static float prevA[3];
static bool havePrev = false;

static bool isModifier(int r, int c) {
  return (r == 2 && (c == 0 || c == 1)) || (r == 3 && c <= 2);
}

static void push(const KeyEvent& e) {
  uint8_t n = (qt + 1) % 24;
  if (n == qh) return;
  q[qt] = e;
  qt = n;
}

static uint16_t mapKey(int r, int c, bool fn, char* chOut, bool shift) {
  Point2D_t pt;
  pt.x = c;
  pt.y = r;
  auto kv = M5Cardputer.Keyboard.getKeyValue(pt);
  char ch = shift ? kv.value_second : kv.value_first;
  bool nav = !textMode || fn;
  *chOut = 0;
  if (r == 0 && c == 13) return K_BACK;
  if (r == 1 && c == 0) return K_TAB;
  if (r == 2 && c == 13) return K_ENTER;
  if (r == 3 && c == 13) { *chOut = ' '; return ' '; }
  if (nav) {
    if (r == 0 && c == 0) return K_ESC;
    if (r == 2 && c == 11) return K_UP;
    if (r == 3 && c == 11) return K_DOWN;
    if (r == 3 && c == 10) return K_LEFT;
    if (r == 3 && c == 12) return K_RIGHT;
  }
  *chOut = ch;
  char lo = kv.value_first;
  return (uint8_t)lo;
}

void inputInit() {
  imuOk = M5.Imu.isEnabled();
}

void inputTextMode(bool on) { textMode = on; }

void inputFlush() { qh = qt = 0; }

void inputPoll() {
  auto& kb = M5Cardputer.Keyboard;
  const auto& list = kb.keyList();
  bool fn = false, shift = false, ctrl = false, alt = false, opt = false;
  for (auto& p : list) {
    if (p.y == 2 && p.x == 0) fn = true;
    if (p.y == 2 && p.x == 1) shift = true;
    if (p.y == 3 && p.x == 0) ctrl = true;
    if (p.y == 3 && p.x == 1) opt = true;
    if (p.y == 3 && p.x == 2) alt = true;
  }
  uint32_t now = millis();
  Held next[8];
  uint8_t nextN = 0;
  for (auto& p : list) {
    if (isModifier(p.y, p.x)) continue;
    uint8_t pos = p.y * 14 + p.x;
    int found = -1;
    for (int i = 0; i < heldN; ++i)
      if (held[i].pos == pos) found = i;
    KeyEvent e{};
    e.shift = shift;
    e.fn = fn;
    e.ctrl = ctrl;
    e.alt = alt;
    e.opt = opt;
    e.key = mapKey(p.y, p.x, fn, &e.ch, shift);
    if (found < 0) {
      push(e);
      if (nextN < 8) next[nextN++] = {pos, now, now + 380};
    } else {
      Held h = held[found];
      if (now >= h.tRep) {
        e.repeat = true;
        push(e);
        h.tRep = now + 70;
      }
      if (nextN < 8) next[nextN++] = h;
    }
  }
  memcpy(held, next, sizeof(Held) * nextN);
  heldN = nextN;

  if (M5.BtnA.wasPressed()) {
    KeyEvent e{};
    e.key = K_G0;
    push(e);
  }

  if (imuOk) {
    float ax, ay, az;
    M5.Imu.update();
    if (M5.Imu.getAccel(&ax, &ay, &az) && !isnan(ax)) {
      // Shake = how much the acceleration vector changed since the last poll, relative to the
      // resting magnitude. Holding still reads ~0 regardless of the IMU's scale or orientation.
      float m = sqrtf(ax * ax + ay * ay + az * az);
      accBase = accBase == 0 ? m : accBase * 0.99f + m * 0.01f;
      if (havePrev && accBase > 0.05f) {
        float dx = ax - prevA[0], dy = ay - prevA[1], dz = az - prevA[2];
        float jerk = sqrtf(dx * dx + dy * dy + dz * dz) / accBase;
        shake = std::max(jerk, shake * 0.86f);
      } else {
        shake *= 0.86f;
      }
      prevA[0] = ax, prevA[1] = ay, prevA[2] = az;
      havePrev = true;
    }
  }
}

bool inputNext(KeyEvent& e) {
  if (qh == qt) return false;
  e = q[qh];
  qh = (qh + 1) % 24;
  return true;
}

#ifdef PERFTEST
uint16_t g_fakeHeld = 0;
#endif
bool keyDown(uint16_t key) {
#ifdef PERFTEST
  if (g_fakeHeld == key) return true;
#endif
  const auto& list = M5Cardputer.Keyboard.keyList();
  bool fn = false;
  for (auto& p : list)
    if (p.y == 2 && p.x == 0) fn = true;
  for (auto& p : list) {
    if (isModifier(p.y, p.x)) continue;
    char ch;
    if (mapKey(p.y, p.x, fn, &ch, false) == key) return true;
  }
  return false;
}

float shakeLevel() { return shake; }
bool imuReady() { return imuOk; }
