#include "r3d.h"
#include <algorithm>

Cam cam;
V3 lightDir = norm(V3(-0.45f, 1.0f, 0.55f));

// ---------------------------------------------------------------- quaternion

Quat Quat::axis(const V3& a, float ang) {
  V3 n = norm(a);
  float s = sinf(ang * 0.5f);
  return {cosf(ang * 0.5f), n.x * s, n.y * s, n.z * s};
}

Quat Quat::operator*(const Quat& o) const {
  return {w * o.w - x * o.x - y * o.y - z * o.z, w * o.x + x * o.w + y * o.z - z * o.y,
          w * o.y - x * o.z + y * o.w + z * o.x, w * o.z + x * o.y - y * o.x + z * o.w};
}

Quat Quat::normalized() const {
  float l = sqrtf(w * w + x * x + y * y + z * z);
  if (l < 1e-9f) return Quat();
  return {w / l, x / l, y / l, z / l};
}

V3 Quat::rot(const V3& v) const {
  V3 u(x, y, z);
  V3 t = cross(u, v) * 2.0f;
  return v + t * w + cross(u, t);
}

Quat Quat::between(const V3& from, const V3& to) {
  V3 a = norm(from), b = norm(to);
  float d = dot(a, b);
  if (d > 0.99999f) return Quat();
  if (d < -0.99999f) {
    V3 ax = cross(V3(1, 0, 0), a);
    if (len(ax) < 1e-3f) ax = cross(V3(0, 0, 1), a);
    return axis(ax, PI);
  }
  V3 c = cross(a, b);
  return Quat(1 + d, c.x, c.y, c.z).normalized();
}

Quat Quat::slerp(const Quat& a, const Quat& b0, float t) {
  Quat b = b0;
  float d = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
  if (d < 0) {
    d = -d;
    b = {-b.w, -b.x, -b.y, -b.z};
  }
  if (d > 0.9995f) {
    return Quat(a.w + (b.w - a.w) * t, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t)
        .normalized();
  }
  float th = acosf(d), s = sinf(th);
  float ka = sinf((1 - t) * th) / s, kb = sinf(t * th) / s;
  return Quat(a.w * ka + b.w * kb, a.x * ka + b.x * kb, a.y * ka + b.y * kb, a.z * ka + b.z * kb);
}

Quat Quat::integrate(const V3& om, float dt) const {
  float a = len(om) * dt;
  if (a < 1e-6f) return *this;
  return (axis(om, a) * (*this)).normalized();
}

// ---------------------------------------------------------------- camera

void Cam::lookAt(const V3& eye, const V3& target, float focal) {
  pos = eye;
  fwd = norm(target - eye);
  right = norm(cross(fwd, V3(0, 1, 0)));
  up = cross(right, fwd);
  f = focal;
}

bool Cam::project(const V3& p, float& sx, float& sy, float& z) const {
  V3 d = p - pos;
  z = dot(d, fwd);
  if (z < 0.05f) return false;
  float iz = f / z;
  sx = cx + dot(d, right) * iz;
  sy = cy - dot(d, up) * iz;
  return true;
}

V3 Cam::ray(float sx, float sy) const {
  return norm(fwd + right * ((sx - cx) / f) - up * ((sy - cy) / f));
}

// ---------------------------------------------------------------- shading

uint16_t shade(const Mat& m, const V3& n, const V3& toEye, float extra) {
  float r = ((m.base >> 11) & 31) / 31.0f, g = ((m.base >> 5) & 63) / 63.0f, b = (m.base & 31) / 31.0f;
  float nl = dot(n, lightDir);
  float diff = std::max(0.0f, nl);
  // sky/ground ambient
  float amb = m.amb * (0.65f + 0.35f * n.y);
  float k = (amb + m.diff * diff) * extra;
  float sp = 0;
  if (m.spec > 0 && nl > 0) {
    V3 h = norm(lightDir + toEye);
    float nh = std::max(0.0f, dot(n, h));
    sp = m.spec * nh / (m.shin - m.shin * nh + nh);  // Schlick's stand-in for pow(nh, shin)
  }
  float R = r * k + sp, G = g * k + sp, B = b * k + sp * 0.92f;
  if (m.rim) {
    float ne = std::max(0.0f, dot(n, toEye));
    float f = 1 - ne;
    float rim = f * f * f * 0.35f;
    R += ((m.rim >> 11) & 31) / 31.0f * rim;
    G += ((m.rim >> 5) & 63) / 63.0f * rim;
    B += (m.rim & 31) / 31.0f * rim;
  }
  return rgb(std::min(255.0f, R * 255), std::min(255.0f, G * 255), std::min(255.0f, B * 255));
}

// ---------------------------------------------------------------- meshes

void Mesh::finish(bool orientOutward, const V3& center) {
  n.resize(mat.size());
  for (size_t i = 0; i < mat.size(); ++i) {
    uint16_t* t = &tri[i * 3];
    V3 a = v[t[0]], b = v[t[1]], c = v[t[2]];
    V3 nn = norm(cross(b - a, c - a));
    if (orientOutward) {
      V3 mid = (a + b + c) * (1.0f / 3);
      if (dot(nn, mid - center) < 0) {
        std::swap(t[1], t[2]);
        nn = -nn;
      }
    }
    n[i] = nn;
  }
}

// Per-vertex scratch, allocated only while a 3D scene is on screen (about 16 KB).
struct Scratch {
  float sx[640], sy[640], sz[640];
  bool ok[640];
  V3 wp[640];
};
static Scratch* scr = nullptr;
#define tsx (scr->sx)
#define tsy (scr->sy)
#define tsz (scr->sz)
#define tok (scr->ok)
#define twp (scr->wp)

static void transform(const Mesh& mesh, const Quat& q, const V3& pos, float scale) {
  if (!scr) scr = (Scratch*)malloc(sizeof(Scratch));
  size_t nv = std::min(mesh.v.size(), (size_t)640);
  for (size_t i = 0; i < nv; ++i) {
    V3 w = q.rot(mesh.v[i] * scale) + pos;
    twp[i] = w;
    tok[i] = cam.project(w, tsx[i], tsy[i], tsz[i]);
  }
}

static inline int px(float v) { return (int)(v + (v >= 0 ? 0.5f : -0.5f)); }

void drawConvex(const Mesh& mesh, const Quat& q, const V3& pos, float scale, const Mat* mats) {
  transform(mesh, q, pos, scale);
  // small objects: one eye direction for the whole mesh is indistinguishable and much cheaper
  V3 toEye = norm(cam.pos - pos);
  const V3* lastN = nullptr;
  int lastM = -1;
  bool lastVis = false;
  uint16_t lastC = 0;
  for (size_t i = 0; i < mesh.mat.size(); ++i) {
    const uint16_t* t = &mesh.tri[i * 3];
    if (!tok[t[0]] || !tok[t[1]] || !tok[t[2]]) continue;
    // consecutive triangles of one flat face (a face, its pips) share normal and colour
    const V3& nl = mesh.n[i];
    if (!lastN || mesh.mat[i] != lastM || nl.x != lastN->x || nl.y != lastN->y || nl.z != lastN->z) {
      V3 nw = q.rot(nl);
      lastVis = dot(nw, toEye) > 0;
      if (lastVis) lastC = shade(mats[mesh.mat[i]], nw, toEye);
      lastN = &nl;
      lastM = mesh.mat[i];
    }
    if (!lastVis) continue;
    cv.fillTriangle(px(tsx[t[0]]), px(tsy[t[0]]), px(tsx[t[1]]), px(tsy[t[1]]), px(tsx[t[2]]), px(tsy[t[2]]), lastC);
  }
}

struct STri {
  int16_t x[3], y[3];
  float z;
  uint16_t c;
};
static std::vector<STri> sorted;

void pushSorted(const Mesh& mesh, const Quat& q, const V3& pos, float scale, const Mat* mats, bool cull) {
  transform(mesh, q, pos, scale);
  if (sorted.capacity() < 900) sorted.reserve(900);
  for (size_t i = 0; i < mesh.mat.size(); ++i) {
    const uint16_t* t = &mesh.tri[i * 3];
    if (!tok[t[0]] || !tok[t[1]] || !tok[t[2]]) continue;
    V3 nw = q.rot(mesh.n[i]);
    V3 toEye = norm(cam.pos - twp[t[0]]);
    float facing = dot(nw, toEye);
    if (cull && facing <= 0) continue;
    if (facing < 0) nw = -nw;  // two-sided
    STri s;
    for (int k = 0; k < 3; ++k) {
      s.x[k] = px(tsx[t[k]]);
      s.y[k] = px(tsy[t[k]]);
    }
    s.z = std::max(tsz[t[0]], std::max(tsz[t[1]], tsz[t[2]])) * 0.5f + (tsz[t[0]] + tsz[t[1]] + tsz[t[2]]) / 6.0f;
    s.c = shade(mats[mesh.mat[i]], nw, toEye);
    sorted.push_back(s);
  }
}

void flushSorted() {
  std::sort(sorted.begin(), sorted.end(), [](const STri& a, const STri& b) { return a.z > b.z; });
  for (auto& s : sorted) cv.fillTriangle(s.x[0], s.y[0], s.x[1], s.y[1], s.x[2], s.y[2], s.c);
  sorted.clear();
}

void floorShadow(const V3& pos, float radius, float height, float strength) {
  float sx, sy, z;
  // shadow slides away from the light a little as the object rises
  V3 p(pos.x - lightDir.x * height * 0.35f, 0, pos.z - lightDir.z * height * 0.35f);
  if (!cam.project(p, sx, sy, z)) return;
  float spread = 1.0f + height * 0.35f;
  float r = radius * spread * cam.f / z;
  float squash = std::max(0.2f, fabsf(cam.fwd.y));
  float k = strength / (1.0f + height * 0.8f);
  softShadow(sx, sy, r * 1.25f, r * squash * 1.25f, k);
}

// ---------------------------------------------------------------- scene background
// A radial vignette computed every frame from squared distance and a small colour ramp:
// no frame-sized cache, so 3D scenes can run while a recording holds its buffers.

static uint16_t ramp[64];
static int bgCx = SW / 2, bgCy = SH / 2;
static bool bgOn = false;

bool sceneBgCreate(uint16_t center, uint16_t edge, float cx, float cy) {
  for (int i = 0; i < 64; ++i) {
    float t = i / 63.0f;
    t = t * t * (3 - 2 * t);
    ramp[i] = __builtin_bswap16(blend565(center, edge, t));
  }
  bgCx = (int)cx;
  bgCy = (int)cy;
  bgOn = true;
  return true;
}

static uint8_t* sqrtLut = nullptr;

void sceneBgDraw() {
  if (!bgOn) {
    cv.fillSprite(col::INK);
    return;
  }
  if (!sqrtLut) {
    sqrtLut = (uint8_t*)malloc(4096);
    if (!sqrtLut) {
      cv.fillSprite(col::INK);
      return;
    }
    for (int i = 0; i < 4096; ++i) sqrtLut[i] = std::min(63, (int)sqrtf((float)i));
  }
  uint16_t* p = (uint16_t*)cv.getBuffer();
  // (dx/rx)^2 + (dy/ry)^2 in 1/4096 units; the ramp ends at 62% of the screen in each axis
  const float rx = SW * 0.62f, ry = SH * 0.62f;
  const int kx = (int)(4096.0f / (rx * rx) * 256), ky = (int)(4096.0f / (ry * ry) * 256);
  for (int y = 0; y < SH; ++y) {
    int dy = y - bgCy;
    int ty = dy * dy * ky;
    uint16_t* row = p + y * SW;
    for (int x = 0; x < SW; ++x) {
      int dx = x - bgCx;
      int d = (dx * dx * kx + ty) >> 8;
      int i = d >= 4095 ? 63 : sqrtLut[d];
      // ordered dither between neighbouring steps hides 565 banding
      if (i < 63 && ((x ^ y) & 1) && (d & 32)) ++i;
      row[x] = ramp[i];
    }
  }
}

void sceneBgFree() {
  bgOn = false;
  free(sqrtLut);
  sqrtLut = nullptr;
  free(scr);
  scr = nullptr;
  sorted.clear();
  sorted.shrink_to_fit();
}
