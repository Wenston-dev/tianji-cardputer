// A tiny flat-shaded software 3D renderer: vectors, quaternions, a perspective camera,
// convex meshes drawn in order with back-face culling, and a depth-sorted triangle list.
#pragma once
#include <Arduino.h>
#include <vector>
#include "gfx.h"

struct V3 {
  float x, y, z;
  V3() : x(0), y(0), z(0) {}
  V3(float a, float b, float c) : x(a), y(b), z(c) {}
  V3 operator+(const V3& o) const { return {x + o.x, y + o.y, z + o.z}; }
  V3 operator-(const V3& o) const { return {x - o.x, y - o.y, z - o.z}; }
  V3 operator*(float k) const { return {x * k, y * k, z * k}; }
  V3 operator-() const { return {-x, -y, -z}; }
  V3& operator+=(const V3& o) { x += o.x; y += o.y; z += o.z; return *this; }
  V3& operator*=(float k) { x *= k; y *= k; z *= k; return *this; }
};
inline float dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(const V3& a, const V3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float len(const V3& a) { return sqrtf(dot(a, a)); }
inline V3 norm(const V3& a) {
  float l = len(a);
  return l > 1e-9f ? a * (1.0f / l) : V3(0, 1, 0);
}

struct Quat {
  float w, x, y, z;
  Quat() : w(1), x(0), y(0), z(0) {}
  Quat(float w_, float x_, float y_, float z_) : w(w_), x(x_), y(y_), z(z_) {}
  static Quat axis(const V3& a, float ang);
  static Quat between(const V3& from, const V3& to);  // shortest arc
  static Quat slerp(const Quat& a, const Quat& b, float t);
  Quat operator*(const Quat& o) const;
  Quat conj() const { return {w, -x, -y, -z}; }
  Quat normalized() const;
  V3 rot(const V3& v) const;
  // integrate an angular velocity (world frame, rad/s)
  Quat integrate(const V3& omega, float dt) const;
};

struct Cam {
  V3 pos, fwd, right, up;
  float f = 160, cx = SW / 2, cy = SH / 2;
  void lookAt(const V3& eye, const V3& target, float focal);
  bool project(const V3& p, float& sx, float& sy, float& z) const;
  V3 ray(float sx, float sy) const;  // world-space direction through a pixel
};

struct Mat {
  uint16_t base;
  float amb, diff, spec, shin;
  uint16_t rim;  // colour of the grazing-angle rim light (0 = none)
};

struct Mesh {
  std::vector<V3> v;
  std::vector<uint16_t> tri;  // 3 indices per triangle
  std::vector<uint8_t> mat;   // material per triangle
  std::vector<V3> n;          // face normals (local)
  int addV(const V3& p) { v.push_back(p); return (int)v.size() - 1; }
  void addTri(int a, int b, int c, uint8_t m) {
    tri.push_back(a); tri.push_back(b); tri.push_back(c); mat.push_back(m);
  }
  void addQuad(int a, int b, int c, int d, uint8_t m) { addTri(a, b, c, m); addTri(a, c, d, m); }
  // Orient every face away from `center` (for convex shapes) and compute normals.
  void finish(bool orientOutward, const V3& center = V3());
  void clear() { v.clear(); tri.clear(); mat.clear(); n.clear(); }
};

extern Cam cam;
extern V3 lightDir;  // points toward the light

uint16_t shade(const Mat& m, const V3& nWorld, const V3& toEye, float extra = 1.0f);

// Convex mesh: back faces culled, drawn in list order (later triangles land on top).
void drawConvex(const Mesh& mesh, const Quat& q, const V3& pos, float scale, const Mat* mats);
// Non-convex: triangles go into a shared list; flushSorted() paints them back to front.
void pushSorted(const Mesh& mesh, const Quat& q, const V3& pos, float scale, const Mat* mats, bool cull = true);
void flushSorted();

// Soft contact shadow on the y=0 floor below `pos`.
void floorShadow(const V3& pos, float radius, float height, float strength = 0.55f);

// Background helpers for 3D scenes: a vignette-lit table, cached once per scene.
bool sceneBgCreate(uint16_t center, uint16_t edge, float cx, float cy);
void sceneBgDraw();
void sceneBgFree();
