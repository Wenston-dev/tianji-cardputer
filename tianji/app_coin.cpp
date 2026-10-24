// 硬币: flip a bronze 开元通宝 cash coin. The faces are ray-cast per pixel with a bump-mapped
// relief so the characters catch the light; the landing wobbles like a spinning coin settling.
#include "app.h"
#include "charge.h"
#include "r3d.h"
#include "sfx.h"

namespace {

constexpr int TS = 96;               // relief texture size
constexpr float CR = 1.0f, CT = 0.07f;  // coin radius, half thickness
constexpr float HOLE = 0.2f;

enum St { REST, FLY, WOBBLE };

class Coin : public App {
 public:
  void enter() override {
    if (!texF) makeTextures();
    if (rim.v.empty()) buildRim();
    sceneBgCreate(rgb(40, 34, 44), rgb(6, 5, 8), SW * 0.5f, SH * 0.6f);
    cam.lookAt(V3(0, 3.6f, 3.2f), V3(0, 0.25f, 0), 132);
    cam.cx = SW / 2;
    cam.cy = SH / 2 + 8;
    st = REST;
    p = V3(0, CT, 0);
    q = Quat::axis(V3(0, 1, 0), 0.3f);
    t = 0;
    lastToss = -9;
    hasResult = false;
    ch.reset();
  }

  void leave() override {
    sceneBgFree();
    free(texF);
    free(texB);
    texF = texB = nullptr;
  }

  bool key(const KeyEvent& e) override {
    if (e.key == ' ' || e.key == K_ENTER) {
      if (st == REST && !e.repeat) ch.tap();
      return true;
    }
    if (e.key == 'c' || e.key == '0') {
      heads = tails = 0;
      histN = 0;
      return true;
    }
    return false;
  }

  void frame(float dt) override {
    t += dt;
    float power = ch.update(dt, st == REST && t - lastToss > 0.6f);
    if (power > 0) toss(power);
    if (st == FLY) fly(dt);
    else if (st == WOBBLE) wobble(dt);
    resT += dt;

    sceneBgDraw();
    float lift = p.y - CT;
    floorShadow(V3(p.x, 0, p.z), 0.95f, lift, 0.7f);
    Quat q0 = q;
    V3 p0 = p;
    if (ch.active) {
      // the coin trembles on the thumb, rising as the flick charges
      float k = easeOutCubic(ch.level);
      p.y += 0.15f + 0.25f * k;
      q = Quat::axis(V3(1, 0, 0), (0.05f + 0.1f * k) * sinf(t * 45)) * q;
    }
    drawCoin();
    q = q0;
    p = p0;
    drawHud();
    ch.draw(t);
  }

 private:
  uint8_t* texF = nullptr;
  uint8_t* texB = nullptr;
  Mesh rim;
  Mat rimMat = {rgb(170, 120, 60), 0.4f, 0.7f, 0.5f, 20, rgb(255, 200, 120)};
  St st = REST;
  V3 p, v, w;
  Quat q, flat;
  float t = 0, lastToss = -9, resT = 0;
  float wobT = 0, theta0 = 0.3f, phi = 0, nextTick = 0;
  bool face = true;  // true = 字 (front) up
  bool hasResult = false;
  int heads = 0, tails = 0, streak = 0;
  bool hist[24];
  int histN = 0;

  void makeTextures() {
    texF = (uint8_t*)malloc(TS * TS);
    texB = (uint8_t*)malloc(TS * TS);
    M5Canvas tc(&cv);
    tc.setColorDepth(lgfx::color_depth_t::grayscale_8bit);
    tc.setPsram(false);
    if (!tc.createSprite(TS, TS)) return;
    float c = TS / 2.0f, s = TS / 2.0f / CR;
    auto base = [&]() {
      tc.fillSprite(0);
      // raised outer rim and the rim round the square hole
      tc.fillCircle(c, c, CR * s, 0xFFFF);
      tc.fillCircle(c, c, 0.86f * s, 0);
      int ho = (int)(0.31f * s), hi = (int)(HOLE * s);
      tc.fillRect(c - ho, c - ho, ho * 2, ho * 2, 0xFFFF);
      tc.fillRect(c - hi, c - hi, hi * 2, hi * 2, 0);
    };
    base();
    tc.setFont(&F24);
    tc.setTextColor(0xFFFF);
    tc.setTextDatum(middle_center);
    // 开元通宝 reads top, bottom, right, left
    float r = 0.58f * s;
    tc.drawString("开", c, c - r);
    tc.drawString("元", c, c + r);
    tc.drawString("通", c + r, c);
    tc.drawString("宝", c - r, c);
    grab(tc, texF);
    base();
    // the crescent "moon mark" above the hole on the reverse
    tc.fillCircle(c, c - 0.5f * s, 0.16f * s, 0xFFFF);
    tc.fillCircle(c, c - 0.56f * s, 0.15f * s, 0);
    grab(tc, texB);
    tc.deleteSprite();
  }

  // luminance -> height, softened with a small blur
  void grab(M5Canvas& tc, uint8_t* out) {
    const uint8_t* tmp = (const uint8_t*)tc.getBuffer();
    for (int y = 0; y < TS; ++y)
      for (int x = 0; x < TS; ++x) {
        int sum = 0, n = 0;
        for (int dy = -1; dy <= 1; ++dy)
          for (int dx = -1; dx <= 1; ++dx) {
            int xx = x + dx, yy = y + dy;
            if (xx < 0 || yy < 0 || xx >= TS || yy >= TS) continue;
            sum += tmp[yy * TS + xx];
            ++n;
          }
        out[y * TS + x] = sum / n;
      }
  }

  void buildRim() {
    const int N = 36;
    int top[N], bot[N];
    for (int i = 0; i < N; ++i) {
      float a = i * 2 * PI / N;
      top[i] = rim.addV(V3(cosf(a) * CR, CT, sinf(a) * CR));
      bot[i] = rim.addV(V3(cosf(a) * CR, -CT, sinf(a) * CR));
    }
    for (int i = 0; i < N; ++i) rim.addQuad(top[i], top[(i + 1) % N], bot[(i + 1) % N], bot[i], 0);
    rim.finish(true, V3());
  }

  Charge ch;

  void toss(float power) {
    lastToss = t;
    st = FLY;
    hasResult = false;
    p = V3(frand(-0.2f, 0.2f), CT + 0.05f, 0.4f);
    v = V3(frand(-0.4f, 0.4f), frand(4.0f, 4.6f) + 2.2f * power, -0.5f);
    V3 axis = q.rot(V3(1, 0, 0));
    axis.y = 0;
    w = norm(axis) * (frand(18, 24) + 16 * power) + V3(0, frand(-2, 2), 0);
    sfxRing(1.25f, 0.35f);
  }

  void fly(float dt) {
    v.y -= 13.0f * dt;
    p += v * dt;
    q = q.integrate(w, dt);
    V3 n = q.rot(V3(0, 1, 0));
    float tilt = acosf(std::min(1.0f, fabsf(n.y)));
    float low = p.y - (CR * sinf(tilt) + CT * cosf(tilt));
    if (low <= 0 && v.y < 0) {
      p.y -= low;
      if (v.y < -2.5f) {
        // one hop on the first strike
        v.y = -v.y * 0.22f;
        w *= 0.35f;
        sfxRing(1.0f, 0.8f);
        return;
      }
      face = n.y > 0;
      flat = Quat::between(n, face ? V3(0, 1, 0) : V3(0, -1, 0)) * q;
      theta0 = std::max(0.12f, std::min(0.45f, tilt));
      wobT = 0;
      phi = atan2f(n.z, n.x);
      st = WOBBLE;
      nextTick = 0;
    }
  }

  void wobble(float dt) {
    wobT += dt;
    float th = theta0 * expf(-wobT / 0.55f);
    // precession speeds up as the tilt dies away (the Euler-disk whirr)
    float rate = 5.0f / sqrtf(std::max(0.02f, th));
    phi += rate * dt;
    V3 axis(cosf(phi), 0, sinf(phi));
    q = Quat::axis(axis, th) * flat;
    p.y = CT * cosf(th) + CR * sinf(th);
    p.x *= 0.98f;
    p.z *= 0.98f;
    if (wobT > nextTick) {
      sfxClack(2.4f + (1 - th / theta0), 0.12f + 0.3f * th / theta0);
      nextTick = wobT + std::max(0.025f, (float)PI / rate);
    }
    if (th < 0.008f) {
      q = flat;
      p.y = CT;
      st = REST;
      hasResult = true;
      resT = 0;
      face ? ++heads : ++tails;
      bool prev = histN ? hist[histN - 1] : !face;
      streak = (histN && prev == face) ? streak + 1 : 1;
      if (histN < 24) hist[histN++] = face;
      else {
        memmove(hist, hist + 1, 23);
        hist[23] = face;
      }
      sfxRing(0.9f, 0.5f);
    }
  }

  void drawCoin() {
    drawConvex(rim, q, p, 1, &rimMat);
    V3 nW = q.rot(V3(0, 1, 0));
    bool front = dot(nW, cam.pos - p) > 0;
    float yPlane = front ? CT : -CT;
    const uint8_t* tex = front ? texF : texB;
    if (!tex) return;
    Quat qc = q.conj();
    V3 oL = qc.rot(cam.pos - p);
    V3 fL = qc.rot(cam.fwd), rL = qc.rot(cam.right), uL = qc.rot(cam.up);
    // screen bounds of the face disc
    float x0 = SW, x1 = 0, y0 = SH, y1 = 0;
    for (int i = 0; i < 16; ++i) {
      float a = i * 2 * PI / 16;
      float sx, sy, z;
      if (cam.project(q.rot(V3(cosf(a) * CR, yPlane, sinf(a) * CR)) + p, sx, sy, z)) {
        x0 = std::min(x0, sx), x1 = std::max(x1, sx), y0 = std::min(y0, sy), y1 = std::max(y1, sy);
      }
    }
    int ix0 = std::max(0, (int)x0 - 1), ix1 = std::min(SW - 1, (int)x1 + 1);
    int iy0 = std::max(0, (int)y0 - 1), iy1 = std::min(SH - 1, (int)y1 + 1);
    uint16_t* fb = (uint16_t*)cv.getBuffer();
    V3 view = -cam.fwd;
    V3 hv = norm(lightDir + view);
    float sgn = front ? 1 : -1;
    const float k = 0.018f;
    V3 ax = q.rot(V3(1, 0, 0)), ay = q.rot(V3(0, 1, 0)), az = q.rot(V3(0, 0, 1));
    const float XL = dot(ax, lightDir), YL = dot(ay, lightDir), ZL = dot(az, lightDir);
    const float XH = dot(ax, hv), YH = dot(ay, hv), ZH = dot(az, hv);
    for (int y = iy0; y <= iy1; ++y) {
      float b = (y - cam.cy) / cam.f;
      V3 rowD = fL - uL * b;
      for (int x = ix0; x <= ix1; ++x) {
        float a = (x - cam.cx) / cam.f;
        V3 d = rowD + rL * a;
        if (fabsf(d.y) < 1e-5f) continue;
        float tt = (yPlane - oL.y) / d.y;
        if (tt <= 0) continue;
        float u = oL.x + d.x * tt, wv = oL.z + d.z * tt;
        float r2 = u * u + wv * wv;
        if (r2 > CR * CR) continue;
        if (fabsf(u) < HOLE && fabsf(wv) < HOLE) continue;  // see through the square hole
        int tx = (int)((u / CR + 1) * (TS - 1) / 2);
        int ty = (int)(((front ? wv : -wv) / CR + 1) * (TS - 1) / 2);
        tx = std::max(1, std::min(TS - 2, tx));
        ty = std::max(1, std::min(TS - 2, ty));
        int hgt = tex[ty * TS + tx];
        float gx = tex[ty * TS + tx + 1] - tex[ty * TS + tx - 1];
        float gy = tex[(ty + 1) * TS + tx] - tex[(ty - 1) * TS + tx];
        // local bump normal, lit through the pre-rotated axes (no quaternion per pixel)
        float nx = -gx * k, nz = -gy * k * sgn;
        float inv = 1.0f / sqrtf(nx * nx + 1 + nz * nz);
        float diff = std::max(0.0f, (nx * XL + sgn * YL + nz * ZL) * inv);
        float s1 = std::max(0.0f, (nx * XH + sgn * YH + nz * ZH) * inv);
        float s2 = s1 * s1, s8 = s2 * s2, s16;
        s8 *= s8;
        s16 = s8 * s8;
        float spec = s16 * s8 * s2 * (0.35f + hgt / 255.0f);  // s1^26
        float hh = hgt / 255.0f;
        // recessed field carries a green patina, the raised relief is worn bright
        float R = lerpf(0.36f, 0.86f, hh), G = lerpf(0.34f, 0.66f, hh), B = lerpf(0.22f, 0.34f, hh);
        float L = 0.3f + 0.85f * diff;
        int r8 = std::min(255.0f, (R * L + spec * 0.9f) * 255);
        int g8 = std::min(255.0f, (G * L + spec * 0.8f) * 255);
        int b8 = std::min(255.0f, (B * L + spec * 0.55f) * 255);
        // soften the silhouette
        if (r2 > 0.94f * CR * CR) {
          uint16_t bg = __builtin_bswap16(fb[y * SW + x]);
          uint16_t c = rgb(r8, g8, b8);
          fb[y * SW + x] = __builtin_bswap16(blend565(c, bg, (r2 / (CR * CR) - 0.94f) / 0.06f * 0.6f));
          continue;
        }
        fb[y * SW + x] = __builtin_bswap16(rgb(r8, g8, b8));
      }
    }
  }

  void drawHud() {
    text("硬币", 6, 2, &F16, col::GOLD);
    char b[48];
    // history strip
    for (int i = 0; i < histN; ++i) {
      int x = SW - 6 - (histN - i) * 6;
      cv.fillCircle(x + 2, 8, 2, hist[i] ? col::GOLD : col::SKY);
    }
    if (hasResult) {
      float e = easeOutBack(clamp01(resT / 0.4f));
      float a = clamp01(resT / 0.25f);
      uint16_t c = face ? col::GOLD : col::SKY;
      text(face ? "正" : "反", 30, 40 - (int)((1 - e) * 10), &F24, blend565(col::INK, c, a), AC);
      if (streak >= 3) {
        snprintf(b, sizeof b, "×%d", streak);
        text(b, 30, 68, &F12, blend565(col::INK, col::CINNABAR, a), AC);
      }
    }
    if (st == REST && !hasResult && !ch.active) text("长按空格", SW / 2, SH - 15, &F12, col::MUTE, AC);
  }
};

}  // namespace

App* makeCoin() { return new Coin(); }
