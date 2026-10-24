// 求签: shake a 3D bamboo tube until one slip jumps out, then read it on a paper card.
#include "app.h"
#include "data_qian.h"
#include "r3d.h"
#include "sfx.h"

namespace {

enum St { IDLE, SHAKE, EJECT, FALL, SLIP, DETAIL };

struct Stick {
  float x, z, top, phase;
};

struct Smoke {
  float x, y, r, a, life;
};

constexpr int NSEG = 18;
constexpr float TR = 0.5f, TRI = 0.44f, TH = 1.6f;

enum MatId { M_BAMBOO, M_NODE, M_LABEL, M_RIM, M_INNER, M_FLOOR };

class Qian : public App {
 public:
  void enter() override {
    build();
    sceneBgCreate(rgb(58, 38, 26), rgb(8, 7, 9), SW * 0.45f, SH * 0.78f);
    cam.lookAt(V3(0, 2.3f, 5.0f), V3(0, 1.0f, 0), 168);
    cam.cx = SW * 0.46f;
    cam.cy = SH * 0.5f;
    st = IDLE;
    t = 0;
    shakeAmt = 0;
    for (auto& s : smoke) s.life = -frand(0, 3);
  }

  void leave() override {
    inner.clear();
    outer.clear();
    for (Mesh* m : {&inner, &outer}) {
      m->v.shrink_to_fit();
      m->tri.shrink_to_fit();
      m->mat.shrink_to_fit();
      m->n.shrink_to_fit();
    }
    sceneBgFree();
  }

  bool key(const KeyEvent& e) override {
    if (st == SLIP || st == DETAIL) {
      if (e.key == K_ENTER) {
        st = st == SLIP ? DETAIL : SLIP;
        detailT = 0;
        sfxTick(0.9f, 0.4f);
        return true;
      }
      if (e.key == 'j') {
        go(APP_JIAO);
        return true;
      }
      if (e.key == ' ') {
        st = IDLE;
        startWipe(false);
        return true;
      }
      if (e.key == K_ESC || e.key == K_BACK) {
        if (st == DETAIL) {
          st = SLIP;
          return true;
        }
        return false;
      }
      return true;
    }
    return st != IDLE && (e.key == ' ' || e.key == K_ENTER);
  }

  void frame(float dt) override {
    t += dt;
    if (st == SLIP || st == DETAIL) {
      slipT += dt;
      detailT += dt;
      st == SLIP ? drawSlip() : drawDetail();
      return;
    }
    update(dt);
    sceneBgDraw();
    drawIncense(dt);
    floorShadow(V3(0, 0, 0.1f), 0.62f, 0, 0.6f);
    if (st == FALL) floorShadow(stC, 0.35f, stC.y, 0.5f);
    drawTube();
    if (st == FALL) drawFallen();
    drawHud();
  }

 private:
  Mesh inner, outer;
  Mat mats[6] = {
      {rgb(196, 160, 92), 0.42f, 0.78f, 0.25f, 18, rgb(255, 214, 150)},
      {rgb(128, 98, 50), 0.42f, 0.7f, 0.15f, 12, 0},
      {rgb(172, 34, 26), 0.4f, 0.75f, 0.3f, 22, rgb(255, 120, 80)},
      {rgb(222, 196, 140), 0.5f, 0.6f, 0.1f, 8, 0},
      {rgb(70, 50, 30), 0.55f, 0.35f, 0.0f, 1, 0},
      {rgb(40, 28, 20), 0.6f, 0.3f, 0.0f, 1, 0},
  };
  Stick sticks[22];
  Smoke smoke[7];
  St st = IDLE;
  float t = 0, shakeAmt = 0, shakeTime = 0, quietTime = 0, ejectAt = 0, nextRattle = 0;
  float ejectT = 0, fallT = 0, restT = 0, slipT = 0, detailT = 0;
  int ejectIdx = 0, lot = 0;
  Quat q;
  V3 stC, stA, stV, stW;
  float stL = 1.9f;
  bool resting = false;

  void build() {
    inner.clear();
    outer.clear();
    const float hs[] = {0, 0.45f, 0.52f, 1.1f, 1.17f, TH};
    const uint8_t bm[] = {M_BAMBOO, M_NODE, M_BAMBOO, M_NODE, M_BAMBOO};
    int ring[6][NSEG];
    for (int h = 0; h < 6; ++h)
      for (int i = 0; i < NSEG; ++i) {
        float a = i * 2 * PI / NSEG;
        ring[h][i] = outer.addV(V3(sinf(a) * TR, hs[h], cosf(a) * TR));
      }
    for (int b = 0; b < 5; ++b)
      for (int i = 0; i < NSEG; ++i) {
        int j = (i + 1) % NSEG;
        uint8_t m = bm[b];
        if (b == 2 && (i == 0 || i == NSEG - 1)) m = M_LABEL;  // red paper label on the front
        outer.addQuad(ring[b][i], ring[b][j], ring[b + 1][j], ring[b + 1][i], m);
      }
    // rim: the front half belongs to the outer pass, the back half is drawn before the sticks
    int innerTopO[NSEG], innerTopI[NSEG], rimO[NSEG], rimI[NSEG], inBot[NSEG];
    for (int i = 0; i < NSEG; ++i) {
      float a = i * 2 * PI / NSEG;
      rimO[i] = outer.addV(V3(sinf(a) * TR, TH, cosf(a) * TR));
      rimI[i] = outer.addV(V3(sinf(a) * TRI, TH, cosf(a) * TRI));
      innerTopO[i] = inner.addV(V3(sinf(a) * TR, TH, cosf(a) * TR));
      innerTopI[i] = inner.addV(V3(sinf(a) * TRI, TH, cosf(a) * TRI));
      inBot[i] = inner.addV(V3(sinf(a) * TRI, 0.1f, cosf(a) * TRI));
    }
    for (int i = 0; i < NSEG; ++i) {
      int j = (i + 1) % NSEG;
      float mid = (i + 0.5f) * 2 * PI / NSEG;
      if (cosf(mid) >= 0) outer.addQuad(rimO[i], rimO[j], rimI[j], rimI[i], M_RIM);
      else inner.addQuad(innerTopO[i], innerTopO[j], innerTopI[j], innerTopI[i], M_RIM);
    }
    outer.finish(true, V3(0, 0.8f, 0));
    // inner wall faces the axis
    size_t rimTris = inner.mat.size();
    for (int i = 0; i < NSEG; ++i) {
      int j = (i + 1) % NSEG;
      inner.addQuad(inBot[i], inBot[j], innerTopI[j], innerTopI[i], M_INNER);
    }
    int c = inner.addV(V3(0, 0.1f, 0));
    for (int i = 0; i < NSEG; ++i) inner.addTri(c, inBot[(i + 1) % NSEG], inBot[i], M_FLOOR);
    inner.finish(true, V3(0, 0.8f, 0));
    for (size_t i = rimTris; i < inner.mat.size(); ++i) {
      if (inner.mat[i] == M_FLOOR) {
        // bottom disc faces up
        if (inner.n[i].y < 0) {
          std::swap(inner.tri[i * 3 + 1], inner.tri[i * 3 + 2]);
          inner.n[i] = -inner.n[i];
        }
        continue;
      }
      std::swap(inner.tri[i * 3 + 1], inner.tri[i * 3 + 2]);
      inner.n[i] = -inner.n[i];
    }
    // the slips
    for (int i = 0; i < 22; ++i) {
      float a = frand(0, 2 * PI), r = sqrtf(frand(0, 1)) * 0.3f;
      sticks[i] = {sinf(a) * r, cosf(a) * r, frand(1.85f, 2.15f), frand(0, 6.28f)};
    }
  }

  bool shakingInput() { return keyDown(' ') || keyDown(K_ENTER) || shakeLevel() > 0.55f; }

  void update(float dt) {
    bool in = shakingInput();
    switch (st) {
      case IDLE:
        if (in) {
          st = SHAKE;
          shakeTime = 0;
          quietTime = 0;
          ejectAt = frand(1.4f, 3.2f);
        }
        break;
      case SHAKE:
        if (in) {
          shakeTime += dt;
          quietTime = 0;
        } else {
          quietTime += dt;
        }
        if (in && t > nextRattle) {
          sfxKnock(frand(1.3f, 2.2f), frand(0.25f, 0.5f));
          nextRattle = t + frand(0.04f, 0.1f);
        }
        if (shakeTime > ejectAt || (quietTime > 0.15f && shakeTime >= 0.9f)) startEject();
        else if (quietTime > 0.15f) {
          st = IDLE;
          toast("再多摇一会儿", col::DIM);
        }
        break;
      case EJECT:
        ejectT += dt;
        if (ejectT > 0.55f) startFall();
        break;
      case FALL:
        simFall(dt);
        break;
      default:
        break;
    }
    float target = (st == SHAKE) ? 1 : (st == EJECT ? 0.7f : 0);
    shakeAmt = approach(shakeAmt, target, st == SHAKE ? 6 : 3, dt);
    float wob = shakeAmt * (0.11f * sinf(t * 55) + 0.05f * sinf(t * 31 + 1));
    q = Quat::axis(V3(1, 0, 0), 0.42f * shakeAmt + wob + 0.03f * sinf(t * 0.9f)) *
        Quat::axis(V3(0, 0, 1), 0.035f * sinf(t * 0.7f) + shakeAmt * 0.05f * sinf(t * 43));
  }

  void startEject() {
    st = EJECT;
    ejectT = 0;
    ejectIdx = esp_random() % 22;
    lot = esp_random() % kLotCount;
    sfxKnock(0.9f, 0.7f);
  }

  // stick endpoints in tube space; the ejected one slides out along its own axis
  void stickEnds(int i, V3& a, V3& b, float extra) {
    const Stick& s = sticks[i];
    a = V3(s.x, 0.15f + extra, s.z);
    b = V3(s.x * 1.35f, s.top + extra, s.z * 1.35f);
  }

  void startFall() {
    V3 a, b;
    stickEnds(ejectIdx, a, b, 0.95f);
    a = q.rot(a);
    b = q.rot(b);
    stC = (a + b) * 0.5f;
    stA = norm(b - a);
    stL = len(b - a);
    stV = V3(frand(-0.25f, 0.25f), 1.3f, 1.5f);
    stW = V3(frand(4, 7), frand(-1, 1), frand(-2, 2));
    st = FALL;
    fallT = 0;
    restT = 0;
    resting = false;
    sfxWhoosh(0.35f);
  }

  void simFall(float dt) {
    fallT += dt;
    int sub = 4;
    float h = dt / sub;
    for (int k = 0; k < sub; ++k) {
      stV.y -= 9.8f * h;
      stC += stV * h;
      stA = norm(Quat::axis(stW, len(stW) * h).rot(stA));
      float lowY = std::min(stC.y + stA.y * stL / 2, stC.y - stA.y * stL / 2) - 0.02f;
      if (lowY < 0) {
        stC.y -= lowY;
        if (stV.y < -0.4f) sfxKnock(frand(1.6f, 2.0f), std::min(1.0f, -stV.y * 0.25f));
        if (stV.y < 0) stV.y = -stV.y * 0.32f;
        stV.x *= 0.7f;
        stV.z *= 0.7f;
        stW *= 0.55f;
        // gravity pulls the stick flat
        stA.y *= 0.8f;
        stA = norm(stA);
      }
    }
    if (len(stV) < 0.15f && stC.y < 0.06f) restT += dt;
    if (restT > 0.2f && !resting) {
      resting = true;
      sfxChime(1.0f, 0.6f);
    }
    if (resting) {
      stA.y *= 0.85f;
      stA = norm(stA);
      stV *= 0.8f;
      stW *= 0.8f;
    }
    if (resting && restT > 1.4f) {
      st = SLIP;
      slipT = 0;
      startWipe(true);
    }
  }

  // A thin flat bar as two triangles: far cheaper than an anti-aliased wedge line.
  static void strip(float x0, float y0, float x1, float y1, float w, uint16_t c) {
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
    if (l < 0.5f || w < 0.9f) {
      cv.drawLine(x0, y0, x1, y1, c);
      return;
    }
    float nx = -dy / l * w * 0.5f, ny = dx / l * w * 0.5f;
    cv.fillTriangle(x0 + nx, y0 + ny, x0 - nx, y0 - ny, x1 + nx, y1 + ny, c);
    cv.fillTriangle(x0 - nx, y0 - ny, x1 - nx, y1 - ny, x1 + nx, y1 + ny, c);
  }

  void drawStick(V3 a, V3 b, float glow) {
    float x0, y0, z0, x1, y1, z1;
    if (!cam.project(a, x0, y0, z0) || !cam.project(b, x1, y1, z1)) return;
    float w = cam.f / ((z0 + z1) * 0.5f) * 0.018f + 0.35f;
    uint16_t body = rgb(222, 198, 146);
    float xm = lerpf(x0, x1, 0.92f), ym = lerpf(y0, y1, 0.92f);
    strip(x0, y0, xm, ym, w, scale565(body, 0.9f + glow * 0.3f));
    strip(xm, ym, x1, y1, w * 0.9f, col::CINNABAR);
    if (glow > 0) glowDot(x1, y1, 8, col::GOLD, 0.4f * glow);
  }

  void drawTube() {
    pushSorted(inner, q, V3(), 1, mats, true);
    flushSorted();
    // sticks, far to near
    int order[22];
    float depth[22];
    for (int i = 0; i < 22; ++i) {
      V3 a, b;
      stickEnds(i, a, b, 0);
      V3 w = q.rot(b);
      depth[i] = dot(w - cam.pos, cam.fwd);
      order[i] = i;
    }
    std::sort(order, order + 22, [&](int a, int b) { return depth[a] > depth[b]; });
    for (int k = 0; k < 22; ++k) {
      int i = order[k];
      if ((st == FALL || st == SLIP) && i == ejectIdx) continue;
      float jig = shakeAmt * fabsf(sinf(t * 19 + sticks[i].phase)) * 0.09f;
      float extra = jig;
      if (st == EJECT && i == ejectIdx) extra = easeOutCubic(ejectT / 0.55f) * 0.95f;
      V3 a, b;
      stickEnds(i, a, b, extra);
      drawStick(q.rot(a), q.rot(b), (st == EJECT && i == ejectIdx) ? 1 : 0);
    }
    pushSorted(outer, q, V3(), 1, mats, true);
    flushSorted();
    // characters on the red label
    V3 lc = q.rot(V3(0, 0.81f, TR + 0.01f));
    V3 ln = q.rot(V3(0, 0, 1));
    float sx, sy, z;
    if (dot(ln, norm(cam.pos - lc)) > 0.3f && cam.project(lc, sx, sy, z)) textV("灵签", sx, sy - 13, &F12, col::GOLD, 13);
  }

  void drawFallen() {
    V3 a = stC - stA * (stL / 2), b = stC + stA * (stL / 2);
    drawStick(a, b, resting ? clamp01(restT * 2) : 0);

  }

  void drawIncense(float dt) {
    // a joss stick on the right, smoke curling up from its ember
    int bx = 206, by = 118, tx = 210, ty = 64;
    cv.drawLine(bx, by, tx, ty, rgb(120, 46, 36));
    cv.drawLine(bx + 1, by, tx + 1, ty, rgb(96, 36, 30));
    cv.fillRect(bx - 9, by, 18, 10, rgb(64, 50, 40));
    cv.fillRect(bx - 11, by - 2, 22, 3, rgb(96, 76, 58));
    float fl = 0.7f + 0.3f * sinf(t * 9) * sinf(t * 5.3f);
    glowDot(tx, ty, 7, col::EMBER, 0.55f * fl);
    cv.fillCircle(tx, ty, 1, col::EMBER);
    for (auto& s : smoke) {
      s.life += dt;
      if (s.life < 0) continue;
      if (s.life > 3.2f) s = {0, 0, 0, 0, -frand(0, 0.6f)};
      float L = s.life;
      float y = ty - L * 18;
      float x = tx + sinf(L * 2.2f + y * 0.05f) * (2 + L * 4);
      float a = (1 - L / 3.2f) * 0.18f;
      glowDot(x, y, 3 + L * 4, rgb(150, 140, 130), a);
    }
  }

  void drawHud() {
    text("求签", 6, 2, &F16, col::GOLD);
    if (st == IDLE) {
      float p = 0.5f + 0.5f * sinf(t * 2.5f);
      text("按住空格", SW / 2, SH - 15, &F12, blend565(col::INK, col::GOLD2, 0.4f + 0.6f * p), AC);
    } else if (st == SHAKE) {
      // rising tension bar
      float k = clamp01(shakeTime / 1.6f);
      cv.fillRoundRect(SW / 2 - 32, SH - 8, 64, 2, 1, col::INK3);
      cv.fillRoundRect(SW / 2 - 32, SH - 8, (int)(64 * k), 2, 1, col::GOLD);
    }
  }

  void paper(int x, int y, int w, int h) {
    cv.fillRoundRect(x + 2, y + 3, w, h, 3, rgb(4, 3, 3));
    cv.fillRoundRect(x, y, w, h, 3, col::PAPER);
    // a few fibres so it reads as rice paper
    uint32_t s = 99;
    for (int i = 0; i < 70; ++i) {
      s = s * 1103515245u + 12345u;
      int fx = x + 3 + (s >> 8) % (w - 8), fy = y + 3 + (s >> 20) % (h - 6);
      int l = 2 + (s >> 4) % 5;
      cv.drawFastHLine(fx, fy, l, (i & 1) ? col::PAPER2 : rgb(240, 232, 212));
    }
    cv.drawRect(x + 4, y + 4, w - 8, h - 8, col::PAPER2);
  }

  void drawSlip() {
    cv.fillSprite(col::INK);
    const Lot& L = kLots[lot];
    int x = 10, y = 6, w = SW - 20, h = SH - 12;
    paper(x, y, w, h);
    char num[24];
    lotNumber(lot + 1, num);
    // right: number column, then the couplet, then the source
    float e = clamp01(slipT / 0.4f);
    textV(num, x + w - 18, y + 9, &F16, blend565(col::PAPER, col::CINNABAR, e), 17, 99, col::PAPER);
    cv.drawFastVLine(x + w - 32, y + 10, h - 20, col::PAPER2);
    float shown = std::max(0.0f, slipT - 0.35f) * 10;
    textV(L.l1, x + w - 46, y + 8, &F16, col::SUMI, 15, shown, col::PAPER);
    textV(L.l2, x + w - 68, y + 8, &F16, col::SUMI, 15, shown - utf8Len(L.l1), col::PAPER);
    float s3 = shown - utf8Len(L.l1) - utf8Len(L.l2);
    if (s3 > 0) textV(L.src, x + w - 88, y + 12, &F12, blend565(col::PAPER, col::MUTE, clamp01(s3 / 3)), 13);
    // left: the reading
    float r = clamp01((s3 - 2) / 4);
    if (r > 0) {
      text("解曰", x + 10, y + 10, &F12, blend565(col::PAPER, col::CINNABAR, r));
      textWrap(L.jie, x + 10, y + 28, 92, &F12, blend565(col::PAPER, col::SUMI, r), 15, 4);
    }
    // the level seal lands with a thump
    float st_ = slipT - 1.0f - (utf8Len(L.l1) + utf8Len(L.l2)) / 10.0f;
    if (st_ > 0) {
      float k = easeOutBack(clamp01(st_ / 0.35f));
      int sz = (int)(30 + (1 - k) * 14);
      seal(x + 12 + (30 - sz) / 2, y + h - 40 + (30 - sz) / 2, sz, kLevelName[L.level], levelColor(L.level));
    }
  }

  void drawDetail() {
    cv.fillSprite(col::INK);
    const Lot& L = kLots[lot];
    char num[24];
    lotNumber(lot + 1, num);
    text(num, 8, 3, &F16, col::BONE);
    int w = textW(num, &F16);
    uint16_t lc = levelColor(L.level);
    roundPanel(16 + w, 5, 36, 16, 8, blend565(col::INK, lc, 0.2f), lc);
    text(kLevelName[L.level], 34 + w, 6, &F12, lc, AC);
    cv.drawFastHLine(8, 24, SW - 16, col::INK3);
    for (int i = 0; i < 5; ++i) {
      float e = easeOutCubic(clamp01((detailT - i * 0.06f) / 0.35f));
      int y = 30 + i * 20;
      int dx = (int)((1 - e) * 14);
      roundPanel(8 + dx, y, 34, 16, 8, blend565(col::INK, levelColor(L.level), 0.18f * e), blend565(col::INK, levelColor(L.level), e));
      text(kAspectName[i], 25 + dx, y + 1, &F12, blend565(col::INK, col::BONE, e), AC);
      text(kAspect[L.level][i], 50 + dx, y + 1, &F12, blend565(col::INK, col::BONE, e));
    }
    hintBar("");
  }
};

}  // namespace

App* makeQian() { return new Qian(); }
