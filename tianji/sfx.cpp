#include "sfx.h"
#include <M5Cardputer.h>
#include "system.h"

static constexpr uint32_t RATE = 16000;
static constexpr int SMALL = 1400;
static constexpr int BIG = 6000;
static int8_t smallBuf[4][SMALL];
static int8_t* bigBuf[1] = {nullptr};
static uint8_t smallIdx = 0, bigIdx = 0;
static uint32_t rng = 0x1234567;

static inline float noise() {
  rng = rng * 1664525u + 1013904223u;
  return ((int32_t)(rng >> 8) & 0xFFFF) / 32768.0f - 1.0f;
}

bool sfxAvailable() { return S.sfx && M5.Speaker.isEnabled() && M5.Speaker.isRunning(); }

void sfxRelease() {
  for (auto& b : bigBuf) {
    free(b);
    b = nullptr;
  }
}

void sfxInit() {
  for (auto& b : bigBuf)
    if (!b) b = (int8_t*)malloc(BIG);
}

struct Mode {
  float f, amp, tau;  // Hz, linear, seconds
};

// Sum of exponentially decaying partials plus a short noise transient. Each partial is a
// rotating phasor and each envelope a running product, so a sample costs a few multiplies
// (per-sample sinf/expf made a long ring take ~400 ms and froze the screen).
static void synth(int8_t* out, int n, const Mode* modes, int nm, float noiseAmt, float noiseTau, float vol) {
  float c[6], s[6], cr[6], sr[6], amp[6], dec[6];
  for (int k = 0; k < nm; ++k) {
    float w = 2 * PI * modes[k].f / RATE;
    cr[k] = cosf(w), sr[k] = sinf(w);
    c[k] = 1, s[k] = 0;
    amp[k] = modes[k].amp;
    dec[k] = expf(-1.0f / (modes[k].tau * RATE));
  }
  float nAmp = noiseAmt, nDec = expf(-1.0f / (noiseTau * RATE));
  const float g = vol * 100;
  for (int i = 0; i < n; ++i) {
    float v = 0;
    for (int k = 0; k < nm; ++k) {
      float s2 = s[k] * cr[k] + c[k] * sr[k];
      c[k] = c[k] * cr[k] - s[k] * sr[k];
      s[k] = s2;
      v += s2 * amp[k];
      amp[k] *= dec[k];
    }
    if (nAmp > 1e-4f) {
      v += noise() * nAmp;
      nAmp *= nDec;
    }
    if (i < 24) v *= i / 24.0f;  // 1.5 ms attack to avoid clicks
    int q = (int)(v * g);
    out[i] = (int8_t)(q > 127 ? 127 : q < -127 ? -127 : q);
  }
}

static void play(int8_t* buf, int n) {
  M5.Speaker.setVolume(S.volume);
  M5.Speaker.playRaw(buf, n, RATE, false, 1, -1, false);
}

void sfxTick(float pitch, float vol) {
  if (!sfxAvailable()) return;
  int8_t* b = smallBuf[smallIdx++ % 4];
  Mode m[] = {{2600 * pitch, 0.6f, 0.006f}, {5200 * pitch, 0.25f, 0.003f}};
  synth(b, 300, m, 2, 0.35f, 0.0015f, vol);
  play(b, 300);
}

void sfxKnock(float pitch, float vol) {
  if (!sfxAvailable()) return;
  int8_t* b = smallBuf[smallIdx++ % 4];
  Mode m[] = {{480 * pitch, 0.8f, 0.035f}, {1240 * pitch, 0.45f, 0.016f}, {2100 * pitch, 0.22f, 0.008f}};
  synth(b, SMALL, m, 3, 0.5f, 0.002f, vol);
  play(b, SMALL);
}

void sfxClack(float pitch, float vol) {
  if (!sfxAvailable()) return;
  int8_t* b = smallBuf[smallIdx++ % 4];
  Mode m[] = {{1750 * pitch, 0.5f, 0.018f}, {2950 * pitch, 0.4f, 0.010f}, {4400 * pitch, 0.25f, 0.006f}};
  synth(b, 900, m, 3, 0.7f, 0.0018f, vol);
  play(b, 900);
}

void sfxRing(float pitch, float vol) {
  if (!sfxAvailable() || !bigBuf[0]) return;
  int8_t* b = bigBuf[0];
  Mode m[] = {{1480 * pitch, 0.55f, 0.42f}, {3430 * pitch, 0.35f, 0.22f}, {6290 * pitch, 0.18f, 0.12f},
              {1495 * pitch, 0.4f, 0.38f}};
  synth(b, BIG, m, 4, 0.5f, 0.002f, vol * 0.8f);
  play(b, BIG);
}

void sfxChime(float pitch, float vol) {
  if (!sfxAvailable() || !bigBuf[0]) return;
  int8_t* b = bigBuf[0];
  Mode m[] = {{880 * pitch, 0.5f, 0.5f}, {1320 * pitch, 0.3f, 0.35f}, {2640 * pitch, 0.12f, 0.2f}};
  synth(b, BIG, m, 3, 0.0f, 0.001f, vol * 0.7f);
  play(b, BIG);
}

void sfxWhoosh(float vol) {
  if (!sfxAvailable() || !bigBuf[0]) return;
  int8_t* b = bigBuf[0];
  const int n = 4800;
  float lp = 0;
  for (int i = 0; i < n; ++i) {
    float t = (float)i / n;
    float env = 4 * t * (1 - t);  // ~sin(pi t), squared below
    env *= env;
    float cut = 0.04f + 0.25f * t;  // brightening sweep
    lp += (noise() - lp) * cut;
    b[i] = (int8_t)std::max(-127.0f, std::min(127.0f, lp * env * vol * 160));
  }
  play(b, n);
}

void sfxBeep(float freq, uint32_t ms) {
  if (!sfxAvailable()) return;
  M5.Speaker.setVolume(S.volume);
  M5.Speaker.tone(freq, ms, -1, false);
}
