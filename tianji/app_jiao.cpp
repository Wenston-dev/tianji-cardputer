// 掷筊: toss two crescent moon blocks. One flat + one round side up = 圣杯 (yes).
#include "app.h"
#include "charge.h"
#include "r3d.h"
#include "sfx.h"

namespace {

constexpr float RC = 0.72f, WMAX = 0.42f, HMAX = 0.25f, TMAX = 1.15f;
constexpr int NT = 16, NS = 6;

struct Block {
  V3 p, v, w;
  Quat q;
  bool settled = true;
  float settleT = 0, rockT = 0, quietT = 0;
  bool domeUp = false;
  Quat from, rest;
  float fromY = 0, restY = 0;
};

struct Spark {
  float x, y, vx, vy, life;
};

enum Result { R_NONE, R_SHENG, R_XIAO, R_YIN };

class Jiao : public App {
 public:
  void enter() override {
    if (mesh.v.empty()) build();
    sceneBgCreate(rgb(60, 30, 26), rgb(8, 6, 8), SW * 0.5f, SH * 0.62f);
    cam.lookAt(V3(0, 3.3f, 3.6f), V3(0, 0.1f, -0.75f), 150);
    cam.cx = SW / 2;
    cam.cy = SH / 2 + 6;
    // start with the pair resting, one of each face up
    for (int i = 0; i < 2; ++i) {
      Block& b = blk[i];
      b.domeUp = i == 0;
      b.rest = Quat::axis(V3(0, 1, 0), i ? 2.6f : 0.4f) * (b.domeUp ? Quat() : Quat::axis(V3(1, 0, 0), PI));
      b.q = b.rest;
      b.restY = b.domeUp ? 0 : HMAX;
      b.p = V3(i ? 0.55f : -0.55f, b.restY, 0);
      b.settled = true;
      b.settleT = 1;
      b.rockT = 10;
    }
    flying = false;
    result = R_NONE;
    t = 0;
    lastToss = -9;
    ch.reset();
  }

  void leave() override {
    sceneBgFree();
    mesh.clear();
    mesh.v.shrink_to_fit();
    mesh.tri.shrink_to_fit();
    mesh.mat.shrink_to_fit();
    mesh.n.shrink_to_fit();
  }

  bool key(const KeyEvent& e) override {
    if (e.key == ' ' || e.key == K_ENTER) {
      if (!flying && !e.repeat) ch.tap();
      return true;
    }
    return false;
  }

  void frame(float dt) override {
    t += dt;
    float power = ch.update(dt, !flying && t - lastToss > 0.8f);
    if (power > 0) toss(power);
    int sub = 4;
    for (int s = 0; s < sub; ++s)
      for (auto& b : blk) step(b, dt / sub);
    if (flying && blk[0].settled && blk[1].settled && blk[0].settleT > 0.3f && blk[1].settleT > 0.3f) finish();
    if (result != R_NONE) resT += dt;

    sceneBgDraw();
    drawFloorPattern();
    for (int i = 0; i < 2; ++i) {
      // while charging, the pair is lifted and trembles in the cupped hands
      Block& b = blk[i];
      V3 p = b.p;
      Quat q = b.q;
      if (ch.active) {
        float k = easeOutCubic(ch.level);
        p.y += 0.25f + k * 0.35f;
        p.z += 0.4f;
        q = Quat::axis(V3(frand(-1, 1), frand(-1, 1), frand(-1, 1)), (0.03f + 0.12f * k) * sinf(t * 40 + i)) * q;
      }
      floorShadow(p, 0.62f, p.y, 0.65f);
      pushSorted(mesh, q, p, 1, mats, true);
    }
    flushSorted();
    ch.draw(t);
    drawSparks(dt);
    drawHud();
  }

 private:
  Mesh mesh;
  Mat mats[2] = {
      {rgb(176, 30, 24), 0.32f, 0.85f, 0.55f, 26, rgb(255, 120, 90)},  // lacquered round back
      {rgb(206, 98, 66), 0.42f, 0.7f, 0.08f, 6, 0},                     // flat face
  };
  Block blk[2];
  Spark sparks[40];
  int nSparks = 0;
  bool flying = false;
  Result result = R_NONE;
  int streak = 0;
  float t = 0, resT = 0, lastToss = -9;
  Result history[8];
  int histN = 0;

  static float widthAt(float th) { return WMAX * powf(std::max(0.0f, cosf(th / TMAX * PI / 2)), 0.7f) + 0.02f; }
  static float heightAt(float th) { return HMAX * powf(std::max(0.0f, cosf(th / TMAX * PI / 2)), 0.55f); }
  static V3 pt(float th, float r, float y) { return V3(r * sinf(th), y, r * cosf(th) - RC); }

  void build() {
    int dome[NT + 1][NS + 1];
    std::vector<V3> hint;
    for (int i = 0; i <= NT; ++i) {
      float th = -TMAX + 2 * TMAX * i / NT;
      float w = widthAt(th), h = heightAt(th);
      for (int j = 0; j <= NS; ++j) {
        float s = (float)j / NS;
        float r = RC - w / 2 + s * w;
        float y = h * powf(std::max(0.0f, sinf(PI * s)), 0.8f);
        dome[i][j] = mesh.addV(pt(th, r, y));
      }
    }
    for (int i = 0; i < NT; ++i) {
      float th = -TMAX + 2 * TMAX * (i + 0.5f) / NT;
      V3 axisPt = pt(th, RC, -0.12f);
      for (int j = 0; j < NS; ++j) {
        mesh.addQuad(dome[i][j], dome[i + 1][j], dome[i + 1][j + 1], dome[i][j + 1], 0);
        V3 m = (mesh.v[dome[i][j]] + mesh.v[dome[i + 1][j + 1]]) * 0.5f;
        hint.push_back(m - axisPt);
        hint.push_back(m - axisPt);
      }
      // flat face strip
      mesh.addQuad(dome[i][0], dome[i][NS], dome[i + 1][NS], dome[i + 1][0], 1);
      hint.push_back(V3(0, -1, 0));
      hint.push_back(V3(0, -1, 0));
    }
    // end caps at the tips
    mesh.finish(false);
    for (size_t i = 0; i < mesh.mat.size(); ++i) {
      if (dot(mesh.n[i], hint[i]) < 0) {
        std::swap(mesh.tri[i * 3 + 1], mesh.tri[i * 3 + 2]);
        mesh.n[i] = -mesh.n[i];
      }
    }
    // centre the mesh on its centre of mass (roughly a third of the dome height)
    for (auto& v : mesh.v) v.y -= 0.0f;
  }

  Charge ch;

  void toss(float power) {
    lastToss = t;
    flying = true;
    result = R_NONE;
    for (int i = 0; i < 2; ++i) {
      Block& b = blk[i];
      b.p = V3(i ? 0.3f : -0.3f, 1.5f, 1.5f);
      float k = 0.55f + 0.6f * power;
      b.v = V3(frand(-0.6f, 0.6f) + (i ? 0.5f : -0.5f), frand(2.2f, 3.0f) * k, frand(-2.6f, -1.9f) * k);
      b.w = V3(frand(-14, 14), frand(-6, 6), frand(-14, 14)) * (0.5f + 0.7f * power);
      b.q = Quat::axis(V3(frand(-1, 1), frand(-1, 1), frand(-1, 1)), frand(0, 6.28f));
      b.settled = false;
      b.settleT = 0;
      b.quietT = 0;
    }
    sfxWhoosh(0.5f);
  }

  float lowest(const Block& b, V3* at) {
    float m = 1e9f;
    for (size_t i = 0; i < mesh.v.size(); i += 2) {
      V3 w = b.q.rot(mesh.v[i]) + b.p;
      if (w.y < m) {
        m = w.y;
        if (at) *at = w;
      }
    }
    return m;
  }

  void step(Block& b, float h) {
    if (b.settled) {
      b.settleT += h;
      float u = easeOutCubic(clamp01(b.settleT / 0.28f));
      Quat base = Quat::slerp(b.from, b.rest, u);
      b.p.y = lerpf(b.fromY, b.restY, u);
      if (!b.domeUp) {
        // a round back keeps rocking for a moment
        b.rockT += h;
        float a = 0.32f * expf(-b.rockT / 0.45f) * sinf(b.rockT * 15);
        V3 ax = base.rot(V3(1, 0, 0));
        b.q = Quat::axis(ax, a) * base;
      } else {
        b.q = base;
      }
      return;
    }
    b.v.y -= 9.8f * h;
    b.p += b.v * h;
    b.q = b.q.integrate(b.w, h);
    // keep them on the table
    if (fabsf(b.p.x) > 2.1f) b.v.x = -b.v.x * 0.5f, b.p.x = copysignf(2.1f, b.p.x);
    if (b.p.z < -1.3f) b.v.z = -b.v.z * 0.5f, b.p.z = -1.3f;
    if (b.p.z > 1.6f) b.v.z = -b.v.z * 0.5f, b.p.z = 1.6f;
    V3 cp;
    float low = lowest(b, &cp);
    if (low < 0) {
      b.p.y -= low;
      float vin = -b.v.y;
      if (vin > 0) {
        if (vin > 0.7f) sfxKnock(frand(0.85f, 1.15f), std::min(1.0f, vin * 0.22f));
        b.v.y = vin * 0.28f;
        V3 r = cp - b.p;
        V3 imp(0, vin * 1.3f, 0);
        b.w += cross(r, imp) * 2.2f;
        b.v.x *= 0.72f;
        b.v.z *= 0.72f;
        b.w *= 0.8f;
      }
    }
    // the other block
    Block& o = (&b == &blk[0]) ? blk[1] : blk[0];
    V3 d = b.p - o.p;
    d.y = 0;
    float dl = len(d);
    if (dl < 0.75f && dl > 1e-3f) {
      V3 n = d * (1 / dl);
      b.p += n * ((0.75f - dl) * 0.5f);
      float vn = dot(b.v - o.v, n);
      if (vn < 0) {
        b.v += n * (-vn * 0.6f);
        if (-vn > 0.8f) sfxKnock(1.3f, 0.4f);
      }
    }
    float sp = len(b.v), ws = len(b.w);
    if (low < 0.03f && sp < 0.35f && ws < 3.5f) b.quietT += h;
    else b.quietT = 0;
    if (b.quietT > 0.12f) settle(b);
  }

  void settle(Block& b) {
    b.settled = true;
    b.settleT = 0;
    b.rockT = 0;
    V3 up = b.q.rot(V3(0, 1, 0));
    b.domeUp = up.y > 0;
    V3 fx = b.q.rot(V3(1, 0, 0));
    float yaw = atan2f(-fx.z, fx.x);
    b.rest = Quat::axis(V3(0, 1, 0), yaw) * (b.domeUp ? Quat() : Quat::axis(V3(1, 0, 0), PI));
    b.from = b.q;
    b.fromY = b.p.y;
    b.restY = b.domeUp ? 0 : HMAX;
  }

  void finish() {
    flying = false;
    int flats = (!blk[0].domeUp) + (!blk[1].domeUp);  // flat side facing up
    result = flats == 1 ? R_SHENG : flats == 2 ? R_XIAO : R_YIN;
    resT = 0;
    streak = result == R_SHENG ? streak + 1 : 0;
    if (histN < 8) history[histN++] = result;
    else {
      memmove(history, history + 1, sizeof(Result) * 7);
      history[7] = result;
    }
    if (result == R_SHENG) sfxChime(1.0f, 0.7f);
    else sfxChime(0.7f, 0.5f);
    if (streak == 3) burst();
  }

  void burst() {
    nSparks = 40;
    for (auto& s : sparks) {
      float a = frand(0, 6.28f), v = frand(30, 90);
      s = {SW / 2.0f, 46, cosf(a) * v, sinf(a) * v - 30, frand(0.8f, 1.6f)};
    }
    sfxChime(1.5f, 0.8f);
  }

  void drawSparks(float dt) {
    for (int i = 0; i < nSparks; ++i) {
      Spark& s = sparks[i];
      if (s.life <= 0) continue;
      s.life -= dt;
      s.vy += 60 * dt;
      s.x += s.vx * dt;
      s.y += s.vy * dt;
      glowDot(s.x, s.y, 4, col::GOLD, clamp01(s.life) * 0.6f);
    }
  }

  void drawFloorPattern() {
    // faint concentric arcs, like a worn temple floor
    float sx, sy, z;
    if (!cam.project(V3(0, 0, 0), sx, sy, z)) return;
    for (int r = 1; r <= 3; ++r) {
      float rr = r * 0.9f * cam.f / z;
      cv.drawEllipse(sx, sy, rr, rr * 0.55f, blend565(rgb(40, 24, 22), rgb(70, 40, 34), 0.5f / r));
    }
  }

  void drawHud() {
    text("掷筊", 6, 2, &F16, col::GOLD);
    // streak of holy cups
    for (int i = 0; i < 3; ++i) {
      int x = SW - 40 + i * 12, y = 11;
      bool on = i < std::min(streak, 3);
      if (on) glowDot(x, y, 6, col::GOLD, 0.3f);
      cv.fillArc(x, y, 4, 2, 0, 180, on ? col::GOLD : col::INK3);
    }
    if (result == R_NONE) {
      if (!flying && !ch.active) {
        float p = 0.5f + 0.5f * sinf(t * 2.5f);
        text("长按空格", SW / 2, SH - 16, &F12, blend565(col::MUTE, col::GOLD, p), AC);
      }
      return;
    }
    static const char* name[] = {"", "圣杯", "笑杯", "阴杯"};
    static const char* desc[] = {"", "应允", "再问", "不允"};
    uint16_t c = result == R_SHENG ? col::GOLD : result == R_XIAO ? col::JADE : col::SKY;
    float e = easeOutBack(clamp01(resT / 0.45f));
    float a = clamp01(resT / 0.3f);
    int y = 26 - (int)((1 - e) * 10);
    if (streak >= 3) {
      text("三圣杯", SW / 2, y, &F24, blend565(col::INK, col::GOLD, a), AC);
      text("所求已准", SW / 2, SH - 16, &F12, blend565(col::INK, col::BONE, a), AC);
    } else {
      text(name[result], SW / 2, y, &F24, blend565(col::INK, c, a), AC);
      text(desc[result], SW / 2, SH - 16, &F12, blend565(col::INK, col::BONE, a), AC);
    }
    // recent tosses
    for (int i = 0; i < histN; ++i) {
      uint16_t hc = history[i] == R_SHENG ? col::GOLD : history[i] == R_XIAO ? col::JADE : col::SKY;
      cv.fillCircle(8 + i * 7, SH - 30, 2, blend565(col::INK, hc, 0.4f + 0.6f * (i + 1) / histN));
    }
  }
};

}  // namespace

App* makeJiao() { return new Jiao(); }
