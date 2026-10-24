#include "store.h"
#include <M5Cardputer.h>
#include <SD.h>
#include <algorithm>
#include "rec.h"
#include "system.h"

namespace store {

static inline uint8_t toDb(int peak) {
  if (peak < 1) return 0;
  float v = (20.0f * log10f(peak / 32768.0f) + 54.0f) / 54.0f;
  v = v < 0 ? 0 : v > 1 ? 1 : v;
  return (uint8_t)(v * 255);
}

void titleFor(const char* path, char* out, size_t n) {
  const char* base = strrchr(path, '/');
  base = base ? base + 1 : path;
  int y, mo, d, h, mi, s, part = 0;
  if (sscanf(base, "%4d%2d%2d_%2d%2d%2d_%d", &y, &mo, &d, &h, &mi, &s, &part) >= 6) {
    if (part > 1) snprintf(out, n, "%d月%d日 %02d:%02d (%d)", mo, d, h, mi, part);
    else snprintf(out, n, "%d月%d日 %02d:%02d", mo, d, h, mi);
    return;
  }
  int k;
  if (sscanf(base, "REC_%d", &k) == 1) {
    snprintf(out, n, "录音 %04d", k);
    return;
  }
  strlcpy(out, base, n);
  char* dot = strrchr(out, '.');
  if (dot) *dot = 0;
}

int list(std::vector<Entry>& out) {
  out.clear();
  if (!rec::sdOk()) return 0;
  File dir = SD.open("/REC");
  if (!dir) return 0;
  std::vector<std::pair<time_t, Entry>> tmp;
  while (File f = dir.openNextFile()) {
    if (f.isDirectory()) continue;
    const char* nm = f.name();
    size_t l = strlen(nm);
    if (l < 5 || strcasecmp(nm + l - 4, ".wav")) continue;
    Entry e{};
    snprintf(e.path, sizeof e.path, "/REC/%s", nm);
    titleFor(e.path, e.title, sizeof e.title);
    rec::WavInfo w;
    if (rec::readWavInfo(f, w)) {
      e.rate = w.rate;
      e.bytes = w.dataBytes;
      e.ms = (uint32_t)((uint64_t)w.dataBytes * 1000 / (w.rate * w.channels * 2));
    }
    tmp.push_back({f.getLastWrite(), e});
    f.close();
  }
  dir.close();
  std::sort(tmp.begin(), tmp.end(), [](const std::pair<time_t, Entry>& a, const std::pair<time_t, Entry>& b) {
    if (a.first != b.first) return a.first > b.first;
    return strcmp(a.second.path, b.second.path) > 0;
  });
  for (auto& p : tmp) out.push_back(p.second);
  return out.size();
}

bool remove(const char* path) {
  rec::sdLock();
  bool ok = SD.remove(path);
  char mp[52];
  strlcpy(mp, path, sizeof mp);
  char* dot = strrchr(mp, '.');
  if (dot) {
    strcpy(dot, ".txt");
    if (SD.exists(mp)) SD.remove(mp);
  }
  rec::sdUnlock();
  return ok;
}

int loadMarks(const char* path, float* out, int max) {
  char mp[52];
  strlcpy(mp, path, sizeof mp);
  char* dot = strrchr(mp, '.');
  if (!dot) return 0;
  strcpy(dot, ".txt");
  rec::sdLock();
  File f = SD.open(mp);
  int n = 0;
  if (f) {
    while (f.available() && n < max) {
      String line = f.readStringUntil('\n');
      if (line.length()) out[n++] = line.toFloat();
    }
    f.close();
  }
  rec::sdUnlock();
  return n;
}

// ---------------------------------------------------------------- overview (background task)

static uint8_t ov[256];
static volatile int ovDone = 0;
static volatile bool ovCancel = false;
static volatile bool ovRunning = false;
static char ovPath[48];
static int ovCols = 0;

static void ovTask(void*) {
  rec::sdLock();
  File f = SD.open(ovPath);
  rec::WavInfo w;
  bool ok = f && rec::readWavInfo(f, w);
  rec::sdUnlock();
  if (ok) {
    static int16_t buf[512];
    uint32_t frames = w.dataBytes / (2 * w.channels);
    for (int c = 0; c < ovCols && !ovCancel; ++c) {
      uint32_t at = (uint64_t)frames * c / ovCols;
      rec::sdLock();
      f.seek(w.dataOffset + at * 2 * w.channels);
      int n = f.read((uint8_t*)buf, sizeof buf) / 2;
      rec::sdUnlock();
      int pk = 0;
      for (int i = 0; i < n; ++i) pk = std::max(pk, abs(buf[i]));
      ov[c] = toDb(pk);
      ovDone = c + 1;
      vTaskDelay(1);
    }
  }
  if (f) {
    rec::sdLock();
    f.close();
    rec::sdUnlock();
  }
  ovRunning = false;
  vTaskDelete(nullptr);
}

void overviewStart(const char* path, int columns) {
  if (ovRunning) {
    ovCancel = true;
    while (ovRunning) delay(2);
  }
  ovCancel = false;
  ovDone = 0;
  ovCols = std::min(columns, 256);
  memset(ov, 0, sizeof ov);
  strlcpy(ovPath, path, sizeof ovPath);
  ovRunning = true;
  xTaskCreatePinnedToCore(ovTask, "ov", 4096, nullptr, 2, nullptr, 0);
}
int overviewDone() { return ovDone; }
const uint8_t* overview() { return ov; }
const char* overviewPath() { return ovPath; }

// ---------------------------------------------------------------- playback

static constexpr int PB = 4096;
static File pf;
static rec::WavInfo pw;
static int16_t* pbuf[3] = {nullptr, nullptr, nullptr};
static int pIdx = 0;
static bool isOpen = false, isPlaying = false;
static uint32_t readFrame = 0, startFrame = 0, startMs = 0, totalFrames = 0;
static int vol = 200;
static uint32_t lvlAt[8];
static uint8_t lvlPk[8];
static int lvlN = 0;

bool playOpen(const char* path) {
  playClose();
  rec::sdLock();
  pf = SD.open(path);
  bool ok = pf && rec::readWavInfo(pf, pw) && pw.channels >= 1 && pw.channels <= 2;
  rec::sdUnlock();
  if (!ok) return false;
  for (auto& b : pbuf)
    if (!b) b = (int16_t*)malloc(PB * 2);
  if (!pbuf[2]) return false;
  totalFrames = pw.dataBytes / (2 * pw.channels);
  readFrame = 0;
  isOpen = true;
  isPlaying = false;
  lvlN = 0;
  M5.Speaker.setChannelVolume(0, vol);
  return true;
}

void playClose() {
  if (isOpen) {
    M5.Speaker.stop(0);
    rec::sdLock();
    pf.close();
    rec::sdUnlock();
  }
  isOpen = isPlaying = false;
  for (auto& b : pbuf) {
    free(b);
    b = nullptr;
  }
}

float playLen() { return isOpen ? (float)totalFrames / pw.rate : 0; }

float playPos() {
  if (!isOpen) return 0;
  uint32_t f = readFrame;
  if (isPlaying) f = std::min<uint32_t>(readFrame, startFrame + (uint64_t)(millis() - startMs) * pw.rate / 1000);
  return (float)f / pw.rate;
}

void playToggle() {
  if (!isOpen) return;
  if (isPlaying) {
    uint32_t at = (uint32_t)(playPos() * pw.rate);
    M5.Speaker.stop(0);
    readFrame = at;
    isPlaying = false;
  } else {
    if (readFrame >= totalFrames) readFrame = 0;
    startFrame = readFrame;
    startMs = millis();
    isPlaying = true;
  }
}

bool playing() { return isPlaying; }

void playSeek(float sec) {
  if (!isOpen) return;
  sec = std::max(0.0f, std::min(sec, playLen() - 0.05f));
  M5.Speaker.stop(0);
  readFrame = (uint32_t)(sec * pw.rate);
  startFrame = readFrame;
  startMs = millis();
  lvlN = 0;
}

void playVolume(int d) {
  vol = std::max(0, std::min(255, vol + d));
  M5.Speaker.setChannelVolume(0, vol);
}
int playVolumeNow() { return vol; }

void playService() {
  if (!isOpen || !isPlaying) return;
  while (M5.Speaker.isPlaying(0) < 2 && readFrame < totalFrames) {
    int16_t* b = pbuf[pIdx];
    uint32_t want = std::min<uint32_t>(PB / pw.channels, totalFrames - readFrame);
    rec::sdLock();
    pf.seek(pw.dataOffset + readFrame * 2 * pw.channels);
    int got = pf.read((uint8_t*)b, want * 2 * pw.channels) / 2;
    rec::sdUnlock();
    if (got <= 0) {
      readFrame = totalFrames;
      break;
    }
    int pk = 0;
    for (int i = 0; i < got; ++i) pk = std::max(pk, abs(b[i]));
    lvlAt[lvlN % 8] = readFrame;
    lvlPk[lvlN % 8] = toDb(pk);
    ++lvlN;
    M5.Speaker.playRaw(b, got, pw.rate, pw.channels == 2, 1, 0, false);
    pIdx = (pIdx + 1) % 3;
    readFrame += got / pw.channels;
  }
  if (readFrame >= totalFrames && !M5.Speaker.isPlaying(0)) {
    isPlaying = false;
    readFrame = 0;
  }
}

float playLevel() {
  if (!isPlaying || !lvlN) return 0;
  uint32_t f = (uint32_t)(playPos() * pw.rate);
  for (int k = 0; k < std::min(lvlN, 8); ++k) {
    int i = (lvlN - 1 - k) % 8;
    if (lvlAt[i] <= f) return lvlPk[i] / 255.0f;
  }
  return 0;
}

}  // namespace store
