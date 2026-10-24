// Fake recording library and USB drive for previews.
#include "Arduino.h"
#include "store.h"
#include "usbdrive.h"

namespace store {
static const char* kNames[] = {"/REC/20261023_153012.wav", "/REC/20261022_091544.wav", "/REC/20261020_201003.wav",
                               "/REC/20261018_140210.wav", "/REC/20261018_140210_2.wav", "/REC/REC_0003.wav"};
static const uint32_t kMs[] = {754000, 3620000, 128000, 14400000, 2210000, 45000};
void titleFor(const char* path, char* out, size_t n) {
  const char* base = strrchr(path, '/') + 1;
  int y, mo, d, h, mi, s, part = 0, k;
  if (sscanf(base, "%4d%2d%2d_%2d%2d%2d_%d", &y, &mo, &d, &h, &mi, &s, &part) >= 6) {
    if (part > 1) snprintf(out, n, "%d月%d日 %02d:%02d (%d)", mo, d, h, mi, part);
    else snprintf(out, n, "%d月%d日 %02d:%02d", mo, d, h, mi);
  } else if (sscanf(base, "REC_%d", &k) == 1) snprintf(out, n, "录音 %04d", k);
}
int list(std::vector<Entry>& out) {
  out.clear();
  for (int i = 0; i < 6; ++i) {
    Entry e{};
    strlcpy(e.path, kNames[i], sizeof e.path);
    titleFor(e.path, e.title, sizeof e.title);
    e.ms = kMs[i];
    e.rate = 16000;
    out.push_back(e);
  }
  return 6;
}
bool remove(const char*) { return true; }
static uint8_t ov[256];
static int cols = 0;
static uint32_t t0 = 0;
void overviewStart(const char*, int c) {
  cols = c;
  t0 = millis();
  for (int i = 0; i < 256; ++i) {
    float k = i * 240.0f / std::max(1, c);
    float env = 0.55f + 0.45f * sinf(k * 0.07f) * sinf(k * 0.023f + 1);
    ov[i] = (uint8_t)std::min(255.0f, env * (150 + 90 * fabsf(sinf(k * 0.9f))) + rand() % 20);
  }
}
int overviewDone() { return std::min(cols, (int)((millis() - t0) / 3)); }
const uint8_t* overview() { return ov; }
const char* overviewPath() { return ""; }
int loadMarks(const char*, float* out, int) { out[0] = 120; out[1] = 410; return 2; }
static bool pl = false;
static float pos = 0;
static uint32_t pt = 0;
bool playOpen(const char*) { pos = 0; return true; }
void playClose() { pl = false; }
void playToggle() { if (!pl) pt = millis(); else pos = playPos(); pl = !pl; }
bool playing() { return pl; }
void playSeek(float s) { pos = s; pt = millis(); }
float playPos() { return pl ? pos + (millis() - pt) / 1000.0f : pos; }
float playLen() { return 754; }
float playLevel() { return 0.6f; }
void playService() {}
void playVolume(int) {}
int playVolumeNow() { return 200; }
}  // namespace store

namespace usbdrive {
bool g_mounted = true;
uint32_t g_r = 0;
bool hostPresent() { return true; }
bool hostArrived() { return false; }
bool begin() { return true; }
bool active() { return true; }
bool mounted() { return g_mounted; }
bool ejected() { return false; }
bool suspended() { return false; }
uint32_t bytesRead() { return g_r += 60000; }
uint32_t bytesWritten() { return 0; }
void exitAndRestart() {}
}  // namespace usbdrive
