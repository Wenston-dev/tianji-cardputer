// 骰子: one to six chamfered ivory dice (red one and four, Chinese style) tumbling on felt.
#include "app.h"
#include "charge.h"
#include "r3d.h"
#include "sfx.h"

namespace {

constexpr float H = 0.5f, CH = 0.11f;
constexpr float XL = 3.3f, ZB = -1.9f, ZF = 1.7f;

struct Die {
  V3 p, v, w;
  Quat q;
  bool settled = true;
  float quietT = 0, settleT = 1;
  Quat from, rest;
  float fromY = H;
  int value = 1;
};

// local face normals and the value painted on them
const V3 kFaceN[6] = {V3(0, 1, 0), V3(0, -1, 0), V3(1, 0, 0), V3(-1, 0, 0), V3(0, 0, 1), V3(0, 0, -1)};
const int kFaceV[6] = {1, 6, 2, 5, 3, 4};

class Dice : public App {
 public:
  void enter() override {
    if (mesh.v.empty()) build();
    sceneBgCreate(rgb(26, 62, 52), rgb(4, 12, 11), SW * 0.5f, SH * 0.55f);
    cam.lookAt(V3(0, 6.6f, 4.4f), V3(0, 0, -0.7f), 170);
    cam.cx = SW / 2;
    cam.cy = SH / 2 + 4;
    arrange();
    t = 0;
    state = REST;
    shown = 0;
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
    if (e.key >= '1' && e.key <= '6' && state == REST) {
      count = e.key - '0';
      arrange();
      sfxClack(1.2f, 0.4f);
      return true;
    }
    if ((e.key == '=' || e.key == '-') && state == REST) {
      count = std::max(1, std::min(6, count + (e.key == '=' ? 1 : -1)));
      arrange();
      sfxClack(1.2f, 0.4f);
      return true;
    }
    if (e.key == ' ' || e.key == K_ENTER) {
      if (state != ROLL && !e.repeat) ch.tap();
      return true;
    }
    return false;
  }

  void frame(float dt) override {
    t += dt;
    float power = ch.update(dt, state == REST);
    if (ch.active && state == REST) {
      state = HOLD;
      holdT = 0;
    }
    if (power > 0) roll(power);
    else if (state == HOLD) {
      holdT += dt;
      rattle(dt);
    }
    if (state == ROLL) {
      const int sub = 4;
      for (int s = 0; s < sub; ++s) {
        for (int i = 0; i < count; ++i) step(d[i], dt / sub);
        for (int k = 0; k < 3; ++k) collide();
      }
      bool all = true;
      for (int i = 0; i < count; ++i) all &= d[i].settled && d[i].settleT > 0.25f;
      if (all) {
        state = REST;
        resT = 0;
        shown = 1;
        sfxChime(1.2f, 0.4f);
      }
    } else {
      for (int i = 0; i < count; ++i) settleAnim(d[i], dt);
    }
    resT += dt;

    sceneBgDraw();
    drawFelt();
    for (int i = 0; i < count; ++i) floorShadow(d[i].p, 0.62f, d[i].p.y - H, 0.6f);
    // painter's order: far dice first
    int order[6];
    for (int i = 0; i < count; ++i) order[i] = i;
    std::sort(order, order + count, [&](int a, int b) { return dot(d[a].p - cam.pos, cam.fwd) > dot(d[b].p - cam.pos, cam.fwd); });
    for (int k = 0; k < count; ++k) drawConvex(mesh, d[order[k]].q, d[order[k]].p, 1, mats);
    drawHud();
    ch.draw(t);
  }

 private:
  enum { REST, HOLD, ROLL } state = REST;
  Charge ch;
  Mesh mesh;
  Mat mats[3] = {
      {rgb(244, 236, 218), 0.42f, 0.7f, 0.32f, 22, rgb(255, 230, 190)},
      {rgb(26, 24, 28), 0.5f, 0.4f, 0.2f, 18, 0},
      {rgb(206, 32, 30), 0.5f, 0.6f, 0.25f, 18, 0},
  };
  Die d[6];
  int count = 1;
  float t = 0, holdT = 0, resT = 0, nextRattle = 0, shown = 0;

  void build() {
    int V[2][2][2][3];
    for (int x = 0; x < 2; ++x)
      for (int y = 0; y < 2; ++y)
        for (int z = 0; z < 2; ++z) {
          float s[3] = {x ? 1.0f : -1.0f, y ? 1.0f : -1.0f, z ? 1.0f : -1.0f};
          for (int a = 0; a < 3; ++a) {
            float c[3];
            for (int k = 0; k < 3; ++k) c[k] = s[k] * (k == a ? H : H - CH);
            V[x][y][z][a] = mesh.addV(V3(c[0], c[1], c[2]));
          }
        }
    auto idx = [&](int a, int sa, int b, int sb, int c, int sc, int which) {
      int s[3];
      s[a] = sa;
      s[b] = sb;
      s[c] = sc;
      return V[s[0]][s[1]][s[2]][which];
    };
    // edge bevels
    for (int a = 0; a < 3; ++a)
      for (int b = a + 1; b < 3; ++b) {
        int c = 3 - a - b;
        for (int sa = 0; sa < 2; ++sa)
          for (int sb = 0; sb < 2; ++sb)
            mesh.addQuad(idx(a, sa, b, sb, c, 0, a), idx(a, sa, b, sb, c, 1, a), idx(a, sa, b, sb, c, 1, b),
                         idx(a, sa, b, sb, c, 0, b), 0);
      }
    // corners
    for (int x = 0; x < 2; ++x)
      for (int y = 0; y < 2; ++y)
        for (int z = 0; z < 2; ++z) mesh.addTri(V[x][y][z][0], V[x][y][z][1], V[x][y][z][2], 0);
    // faces, each followed by its pips so they paint on top
    static const int8_t pips[7][6][2] = {
        {},
        {{0, 0}},
        {{-1, -1}, {1, 1}},
        {{-1, -1}, {0, 0}, {1, 1}},
        {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}},
        {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}, {0, 0}},
        {{-1, -1}, {-1, 0}, {-1, 1}, {1, -1}, {1, 0}, {1, 1}},
    };
    for (int f = 0; f < 6; ++f) {
      V3 n = kFaceN[f];
      int a = n.x != 0 ? 0 : n.y != 0 ? 1 : 2;
      int sa = (n.x + n.y + n.z) > 0 ? 1 : 0;
      int b = (a + 1) % 3, c = (a + 2) % 3;
      mesh.addQuad(idx(a, sa, b, 0, c, 0, a), idx(a, sa, b, 1, c, 0, a), idx(a, sa, b, 1, c, 1, a),
                   idx(a, sa, b, 0, c, 1, a), 0);
      int val = kFaceV[f];
      bool red = val == 1 || val == 4;
      float pr = val == 1 ? 0.15f : 0.078f;
      for (int k = 0; k < val; ++k) {
        float pu = pips[val][k][0] * 0.25f, pv = pips[val][k][1] * 0.25f;
        float base[3];
        base[a] = sa ? H + 0.001f : -H - 0.001f;
        base[b] = pu;
        base[c] = pv;
        int cIdx = mesh.addV(V3(base[0], base[1], base[2]));
        int ring[8];
        for (int r = 0; r < 8; ++r) {
          float an = r * 2 * PI / 8;
          float p[3] = {base[0], base[1], base[2]};
          p[b] += cosf(an) * pr;
          p[c] += sinf(an) * pr;
          ring[r] = mesh.addV(V3(p[0], p[1], p[2]));
        }
        for (int r = 0; r < 8; ++r) mesh.addTri(cIdx, ring[r], ring[(r + 1) % 8], red ? 2 : 1);
      }
    }
    mesh.finish(true, V3());
  }

  // Grid slot for die i: up to three per row, rows centred, generous spacing.
  V3 slot(int i) const {
    int rows = (count + 2) / 3;
    int row = i / 3;
    int inRow = std::min(3, count - row * 3);
    float cx = (i % 3 - (inRow - 1) / 2.0f) * 1.9f;
    float cz = (row - (rows - 1) / 2.0f) * 1.8f - 0.3f;
    return V3(cx, H, cz);
  }

  void arrange() {
    for (int i = 0; i < count; ++i) {
      Die& x = d[i];
      x.p = slot(i);
      x.q = Quat::axis(V3(0, 1, 0), frand(-0.5f, 0.5f));
      x.rest = x.q;
      x.from = x.q;
      x.settled = true;
      x.settleT = 1;
      x.value = 1;
    }
    shown = 0;
  }

  void rattle(float dt) {
    // dice jostle in the cupped hand above the table
    for (int i = 0; i < count; ++i) {
      Die& x = d[i];
      V3 s = slot(i);
      V3 target(s.x * 0.8f, 2.0f, 1.0f + s.z * 0.5f);
      x.p = V3(lerpf(x.p.x, target.x, 0.2f), lerpf(x.p.y, target.y, 0.2f), lerpf(x.p.z, target.z, 0.2f)) +
            V3(frand(-0.06f, 0.06f), frand(-0.06f, 0.06f), frand(-0.06f, 0.06f));
      float k = 0.4f + ch.level;
      x.q = x.q.integrate(V3(frand(-12, 12), frand(-12, 12), frand(-12, 12)) * k, dt);
    }
    if (t > nextRattle) {
      sfxClack(frand(1.1f, 1.6f), frand(0.2f, 0.4f));
      nextRattle = t + frand(0.05f, 0.11f) / (0.6f + ch.level);
    }
  }

  void roll(float power) {
    bool fromHand = state == HOLD;
    state = ROLL;
    shown = 0;
    for (int i = 0; i < count; ++i) {
      Die& x = d[i];
      // each die leaves from its own lane so they rarely meet in the air
      V3 s = slot(i);
      if (!fromHand) x.p = V3(s.x * 0.8f, 2.0f, 1.0f + s.z * 0.5f);
      float k = 0.55f + 0.6f * power;
      x.v = V3(s.x * 0.25f + frand(-0.35f, 0.35f), frand(1.0f, 2.2f) * k, frand(-4.2f, -3.2f) * k + s.z * 0.4f);
      x.w = V3(frand(-18, 18), frand(-10, 10), frand(-18, 18)) * (0.5f + 0.7f * power);
      x.settled = false;
      x.quietT = 0;
    }
    sfxWhoosh(0.4f);
  }

  void step(Die& x, float h) {
    if (x.settled) {
      settleAnim(x, h);
      return;
    }
    x.v.y -= 16.0f * h;
    x.p += x.v * h;
    x.q = x.q.integrate(x.w, h);
    // the lowest corner touches the felt
    float low = 1e9f;
    V3 cp;
    for (int c = 0; c < 8; ++c) {
      V3 l((c & 1) ? H : -H, (c & 2) ? H : -H, (c & 4) ? H : -H);
      V3 w = x.q.rot(l * 0.93f) + x.p;
      if (w.y < low) low = w.y, cp = w;
    }
    if (low < 0) {
      x.p.y -= low;
      float vin = -x.v.y;
      if (vin > 0) {
        if (vin > 1.0f) sfxClack(frand(0.9f, 1.2f), std::min(1.0f, vin * 0.15f));
        x.v.y = vin * 0.38f;
        V3 r = cp - x.p;
        x.w += cross(r, V3(0, vin, 0)) * 1.6f;
        x.w *= 0.78f;
        x.v.x *= 0.8f;
        x.v.z *= 0.8f;
      } else {
        x.v.x *= 0.96f;
        x.v.z *= 0.96f;
        x.w *= 0.94f;
      }
    }
    if (x.p.x > XL) x.p.x = XL, x.v.x = -fabsf(x.v.x) * 0.5f, x.w.z += 4;
    if (x.p.x < -XL) x.p.x = -XL, x.v.x = fabsf(x.v.x) * 0.5f, x.w.z -= 4;
    if (x.p.z < ZB) x.p.z = ZB, x.v.z = fabsf(x.v.z) * 0.5f, x.w.x += 4;
    if (x.p.z > ZF) x.p.z = ZF, x.v.z = -fabsf(x.v.z) * 0.5f;
    if (low < 0.04f && len(x.v) < 0.5f && len(x.w) < 2.5f) x.quietT += h;
    else x.quietT = 0;
    if (x.quietT > 0.1f) settle(x);
  }

  // Sphere-sphere separation in 3D. A die at rest never moves; a moving one is pushed off it.
  void collide() {
    const float minD = 1.18f;
    for (int i = 0; i < count; ++i)
      for (int j = i + 1; j < count; ++j) {
        Die& A = d[i];
        Die& B = d[j];
        if (A.settled && B.settled) continue;
        V3 dd = A.p - B.p;
        float l = len(dd);
        if (l >= minD || l < 1e-4f) continue;
        V3 n = dd * (1 / l);
        float push = minD - l;
        float wa = A.settled ? 0 : (B.settled ? 1 : 0.5f);
        float wb = 1 - wa;
        A.p += n * (push * wa);
        B.p += n * (-push * wb);
        float vn = dot(A.v - B.v, n);
        if (vn < 0) {
          float jimp = -vn * 0.8f;
          if (!A.settled) A.v += n * (jimp * (B.settled ? 1.0f : 0.5f));
          if (!B.settled) B.v += n * (-jimp * (A.settled ? 1.0f : 0.5f));
          // glancing hits make them spin
          if (!A.settled) A.w += cross(n, V3(0, 1, 0)) * (jimp * 3);
          if (!B.settled) B.w += cross(n, V3(0, -1, 0)) * (jimp * 3);
          if (-vn > 1.2f) sfxClack(1.5f, 0.35f);
        }
        if (A.p.y < H) A.p.y = H;
        if (B.p.y < H) B.p.y = H;
      }
  }

  void settle(Die& x) {
    int best = 0;
    float by = -2;
    for (int f = 0; f < 6; ++f) {
      float y = x.q.rot(kFaceN[f]).y;
      if (y > by) by = y, best = f;
    }
    x.value = kFaceV[best];
    x.from = x.q;
    x.rest = Quat::between(x.q.rot(kFaceN[best]), V3(0, 1, 0)) * x.q;
    x.fromY = x.p.y;
    x.settled = true;
    x.settleT = 0;
  }

  void settleAnim(Die& x, float h) {
    if (x.settleT >= 1) return;
    x.settleT += h;
    float u = easeOutCubic(clamp01(x.settleT / 0.22f));
    x.q = Quat::slerp(x.from, x.rest, u);
    x.p.y = lerpf(x.fromY, H, u);
  }

  void drawFelt() {
    // stitched border of the dice tray
    V3 c[4] = {V3(-XL - 0.6f, 0, ZB - 0.6f), V3(XL + 0.6f, 0, ZB - 0.6f), V3(XL + 0.6f, 0, ZF + 0.6f), V3(-XL - 0.6f, 0, ZF + 0.6f)};
    float sx[4], sy[4], z;
    for (int i = 0; i < 4; ++i) cam.project(c[i], sx[i], sy[i], z);
    for (int i = 0; i < 4; ++i) {
      int j = (i + 1) % 4;
      for (float u = 0; u < 1; u += 0.02f) {
        float x = lerpf(sx[i], sx[j], u), y = lerpf(sy[i], sy[j], u);
        if (((int)(u * 50)) & 1) cv.drawPixel(x, y, rgb(52, 98, 82));
      }
    }
  }

  void drawHud() {
    text("骰子", 6, 2, &F16, col::GOLD);
    char b[32];
    // dice count pips
    for (int i = 0; i < 6; ++i) {
      int x = 46 + i * 7;
      if (i < count) cv.fillRoundRect(x, 9, 5, 5, 1, col::BONE);
      else cv.drawRoundRect(x, 9, 5, 5, 1, col::INK3);
    }
    if (state == REST && shown > 0) {
      int sum = 0;
      int cnt[7] = {0};
      for (int i = 0; i < count; ++i) sum += d[i].value, cnt[d[i].value]++;
      float e = easeOutBack(clamp01(resT / 0.4f));
      float a = clamp01(resT / 0.25f);
      snprintf(b, sizeof b, "%d", sum);
      textMono(b, SW - 8, (int)(2 - (1 - e) * 8), &FBIG, blend565(col::INK, col::WHITE, a), 19, AR);
      const char* tag = nullptr;
      int maxSame = 0;
      for (int v = 1; v <= 6; ++v) maxSame = std::max(maxSame, cnt[v]);
      if (count >= 3 && maxSame == count) tag = "豹子";
      else if (count == 3) {
        int mn = 7, mx = 0;
        for (int i = 0; i < 3; ++i) mn = std::min(mn, d[i].value), mx = std::max(mx, d[i].value);
        if (maxSame == 1 && mx - mn == 2) tag = "顺子";
        else if (maxSame == 2) tag = "对子";
      } else if (count == 2 && maxSame == 2) tag = "对子";
      if (tag) {
        float p = 0.6f + 0.4f * sinf(t * 5);
        int w = textW(tag, &F16) + 14;
        int x = SW - 8 - w, y = 44;
        roundPanel(x, y, w, 20, 10, blend565(col::INK, col::CINNABAR, 0.25f * a), blend565(col::INK, col::CINNABAR, a * p));
        text(tag, x + w / 2, y + 2, &F16, blend565(col::INK, col::CINNABAR, a), AC);
      }
    }
    if (state == REST && shown == 0 && !ch.active) text("长按空格", SW / 2, SH - 15, &F12, col::MUTE, AC);
  }
};

}  // namespace

App* makeDice() { return new Dice(); }
