#include "rec.h"
#include <M5Cardputer.h>
#include <SD.h>
#include <SPI.h>
#include <atomic>
#include "system.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"

namespace rec {

const Preset kPresets[3] = {
    {"随手录", "标准录音", -1, 0xFFFF},
    {"长时录音", "16kHz 8秒息屏", 0, 8},
    {"高保真", "44.1kHz 音乐", 3, 0xFFFF},
};

static constexpr int SD_SCK = 40, SD_MISO = 39, SD_MOSI = 14, SD_CS = 12;
static constexpr int SLOT = 2048;  // samples per capture buffer
static constexpr uint32_t ROLLOVER = 3900000000u;  // stay below FAT32's 4 GiB file limit

static bool sd = false;
static SemaphoreHandle_t sdMtx = nullptr;
static uint64_t freeAtStart = 0;

static volatile State st = IDLE;
static int presetI = 0;
static uint32_t rate = 16000;
static char filePath[48];
static char baseName[40];
static int part = 1;
static File file;
static const char* err = "";
static char errBuf[40];

static int16_t* ring = nullptr;
static int slots = 0;
static QueueHandle_t q = nullptr;
static volatile bool capRun = false;
static volatile bool paused = false;
static volatile bool writerDone = false;
static std::atomic<uint32_t> written{0};
static volatile uint64_t bytes = 0;      // bytes in the current file's data chunk
static volatile uint64_t totalBytes = 0;  // across rollover parts
static volatile uint32_t overrunN = 0;

static float marks[64];
static int markN = 0;

static bool finished = false;
static char finishedPath[48];
static uint32_t finishedMs = 0;

// visuals
static uint8_t hist[512];
static volatile uint32_t histN = 0;
static volatile float peakSm = 0;
static int16_t scopeBuf[128];
static int blockLen = 500, blockCnt = 0, blockPeak = 0;

fs::FS& fs() { return SD; }
void sdLock() {
  if (sdMtx) xSemaphoreTake(sdMtx, portMAX_DELAY);
}
void sdUnlock() {
  if (sdMtx) xSemaphoreGive(sdMtx);
}

bool sdBegin() {
  if (!sdMtx) sdMtx = xSemaphoreCreateMutex();
  if (sd) return true;
  sdLock();
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  sd = SD.begin(SD_CS, SPI, 25000000) && SD.cardType() != CARD_NONE;
  if (sd) {
    if (!SD.exists("/REC")) SD.mkdir("/REC");
    freeAtStart = SD.totalBytes() - SD.usedBytes();
  }
  sdUnlock();
  return sd;
}

bool sdOk() { return sd; }
uint64_t sdFreeBytes() {
  uint64_t used = totalBytes;
  return freeAtStart > used ? freeAtStart - used : 0;
}

static void writeHeader(File& f, uint32_t sr, uint32_t dataLen) {
  uint8_t h[44];
  auto u32 = [&](int o, uint32_t v) { memcpy(h + o, &v, 4); };
  auto u16 = [&](int o, uint16_t v) { memcpy(h + o, &v, 2); };
  memcpy(h, "RIFF", 4);
  u32(4, 36 + dataLen);
  memcpy(h + 8, "WAVEfmt ", 8);
  u32(16, 16);
  u16(20, 1);
  u16(22, 1);
  u32(24, sr);
  u32(28, sr * 2);
  u16(32, 2);
  u16(34, 16);
  memcpy(h + 36, "data", 4);
  u32(40, dataLen);
  f.seek(0);
  f.write(h, 44);
}

static bool openFile() {
  if (part == 1) snprintf(filePath, sizeof filePath, "/REC/%s.wav", baseName);
  else snprintf(filePath, sizeof filePath, "/REC/%s_%d.wav", baseName, part);
  file = SD.open(filePath, FILE_WRITE);
  if (!file) return false;
  writeHeader(file, rate, 0);
  bytes = 0;
  return true;
}

static void finalizeFile() {
  writeHeader(file, rate, (uint32_t)bytes);
  file.close();
  if (markN && part == 1) {
    char mp[52];
    strlcpy(mp, filePath, sizeof mp);
    char* dot = strrchr(mp, '.');
    if (dot) strcpy(dot, ".txt");
    File m = SD.open(mp, FILE_WRITE);
    if (m) {
      for (int i = 0; i < markN; ++i) m.printf("%.1f\n", marks[i]);
      m.close();
    }
  }
}

static inline uint8_t toDb(int peak) {
  if (peak < 1) return 0;
  float db = 20.0f * log10f(peak / 32768.0f);
  float v = (db + 54.0f) / 54.0f;
  v = v < 0 ? 0 : v > 1 ? 1 : v;
  return (uint8_t)(v * 255);
}

static void analyze(const int16_t* d) {
  int bufPeak = 0;
  for (int i = 0; i < SLOT; ++i) {
    int a = abs(d[i]);
    if (a > bufPeak) bufPeak = a;
    if (a > blockPeak) blockPeak = a;
    if (++blockCnt >= blockLen) {
      hist[histN % 512] = toDb(blockPeak);
      ++histN;
      blockCnt = 0;
      blockPeak = 0;
    }
  }
  float p = toDb(bufPeak) / 255.0f;
  peakSm = std::max(p, peakSm * 0.82f);
  for (int i = 0; i < 128; ++i) scopeBuf[i] = d[i * (SLOT / 128)];
}

static void handoff(int slot) {
  analyze(ring + slot * SLOT);
  if (paused) {
    written++;
    return;
  }
  xQueueSend(q, &slot, portMAX_DELAY);
}

// M5Unified's Mic_Class reads a constant on the Cardputer ADV with the IDF 5.5 core, so the
// ES8311 is driven directly: the codec is programmed over I2C, and an i2s_std RX channel
// (Philips, 16-bit stereo, the codec clocks itself from BCLK x8 = 256 fs) supplies the samples.
static i2s_chan_handle_t rx = nullptr;
static int16_t* stereo = nullptr;

static void es(uint8_t reg, uint8_t val) { M5.In_I2C.writeRegister8(0x18, reg, val, 400000); }

static bool micOpen(uint32_t sampleRate) {
  uint8_t pga = kGains[S.gainIdx];  // 3 dB steps, 0..10
  es(0x00, 0x80);  // CSM on
  es(0x01, 0xBA);  // MCLK from BCLK, ADC clocks on
  es(0x02, 0x18);  // MULT_PRE = 3 (x8)
  es(0x09, 0x0C);  // SDP in: I2S 16-bit
  es(0x0A, 0x0C);  // SDP out: I2S 16-bit
  es(0x0D, 0x01);  // analog power up
  es(0x0E, 0x0A);  // PGA + ADC modulator on
  es(0x14, 0x10 | pga);  // MIC1P/N, PGA gain
  es(0x16, 0x04);  // ADC scale
  es(0x17, S.gainIdx == 3 ? 0xD7 : 0xBF);  // ADC volume (0xBF = 0 dB)
  es(0x1C, 0x6A);  // HPF on, EQ bypass

  // The speaker's I2S channel leaves the shared BCLK/WS/data pins routed to itself; a new RX
  // channel then reads a constant. Free the pins first and reuse the speaker's own port.
  for (int pin : {41, 42, 43, 46}) gpio_reset_pin((gpio_num_t)pin);
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
  cc.dma_desc_num = 8;
  cc.dma_frame_num = 256;
  if (i2s_new_channel(&cc, nullptr, &rx) != ESP_OK) return false;
  i2s_std_config_t sc = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sampleRate),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
      .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = GPIO_NUM_41, .ws = GPIO_NUM_43, .dout = I2S_GPIO_UNUSED,
                   .din = GPIO_NUM_46, .invert_flags = {false, false, false}}};
  if (i2s_channel_init_std_mode(rx, &sc) != ESP_OK || i2s_channel_enable(rx) != ESP_OK) {
    i2s_del_channel(rx);
    rx = nullptr;
    return false;
  }
  return true;
}

static void micClose() {
  if (rx) {
    i2s_channel_disable(rx);
    i2s_del_channel(rx);
    rx = nullptr;
  }
  es(0x0E, 0x6A);  // PGA + modulator off
  for (int pin : {41, 42, 43, 46}) gpio_reset_pin((gpio_num_t)pin);  // hand the pins back clean
}

static void captureTask(void*) {
  uint32_t n = 0;
  size_t br;
  // let the codec's filters settle before keeping anything
  for (int k = 0; k < 3; ++k) i2s_channel_read(rx, stereo, SLOT * 4, &br, 200);
  while (capRun) {
    // the slot we are about to reuse must already be on the card
    while (written.load() + slots <= n && capRun) {
      overrunN++;
      vTaskDelay(2);
    }
    if (!capRun) break;
    int slot = n % slots;
    int16_t* dst = ring + slot * SLOT;
    int got = 0;
    while (got < SLOT && capRun) {
      if (i2s_channel_read(rx, stereo, (SLOT - got) * 4, &br, 200) != ESP_OK) continue;
      int frames = br / 4;
      for (int i = 0; i < frames; ++i) dst[got + i] = stereo[i * 2];  // both slots carry the mic
      got += frames;
    }
    if (got < SLOT) memset(dst + got, 0, (SLOT - got) * 2);
    handoff(slot);
    ++n;
  }
  int stopTok = -1;
  xQueueSend(q, &stopTok, portMAX_DELAY);
  vTaskDelete(nullptr);
}

static void writerTask(void*) {
  uint64_t sinceHeader = 0;
  for (;;) {
    int slot;
    xQueueReceive(q, &slot, portMAX_DELAY);
    if (slot < 0) break;
    sdLock();
    size_t w = file.write((const uint8_t*)(ring + slot * SLOT), SLOT * 2);
    if (w != SLOT * 2) err = "写入失败";
    bytes += w;
    totalBytes += w;
    sinceHeader += w;
    if (sinceHeader >= rate * 2 * 5) {  // every ~5 s keep the header valid and flush
      sinceHeader = 0;
      writeHeader(file, rate, (uint32_t)bytes);
      file.seek(44 + bytes);
      file.flush();
    }
    if (bytes >= ROLLOVER) {
      finalizeFile();
      ++part;
      if (!openFile()) err = "无法创建续写文件";
    }
    sdUnlock();
    written++;
  }
  sdLock();
  finalizeFile();
  sdUnlock();
  writerDone = true;
  vTaskDelete(nullptr);
}

static void makeBaseName() {
  struct tm t;
  if (timeLocal(t)) {
    snprintf(baseName, sizeof baseName, "%04d%02d%02d_%02d%02d%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);
    return;
  }
  for (int i = 1; i < 10000; ++i) {
    snprintf(baseName, sizeof baseName, "REC_%04d", i);
    char p[48];
    snprintf(p, sizeof p, "/REC/%s.wav", baseName);
    if (!SD.exists(p)) return;
  }
}

bool start(int preset) {
  if (st != IDLE) return false;
  err = "";
  if (!sdBegin()) {
    err = "未检测到 SD 卡";
    return false;
  }
  presetI = preset;
  const Preset& p = kPresets[preset];
  rate = kRates[p.rateIdx >= 0 ? p.rateIdx : S.rateIdx];

  // the speaker holds DMA buffers and a task; release them before asking for the ring
  M5.Speaker.end();
  for (slots = 8; slots >= 4; slots -= 2) {
    ring = (int16_t*)heap_caps_malloc(slots * SLOT * 2, MALLOC_CAP_8BIT);
    if (ring) break;
  }
  if (!ring) {
    snprintf(errBuf, sizeof errBuf, "内存不足 %uK", (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024));
    err = errBuf;
    M5.Speaker.begin();
    return false;
  }
  sdLock();
  makeBaseName();
  part = 1;
  bool ok = openFile();
  sdUnlock();
  if (!ok) {
    heap_caps_free(ring);
    ring = nullptr;
    err = "无法创建文件";
    M5.Speaker.begin();
    return false;
  }

  // the codec is programmed over the shared internal I2C bus: do it here on the main thread,
  // never from the capture task, so it can't collide with keyboard/IMU polling
  if (!stereo) stereo = (int16_t*)heap_caps_malloc(SLOT * 4, MALLOC_CAP_8BIT);
  if (!stereo || !micOpen(rate)) {
    sdLock();
    file.close();
    SD.remove(filePath);
    sdUnlock();
    heap_caps_free(ring);
    ring = nullptr;
    M5.Speaker.begin();
    err = "麦克风启动失败";
    return false;
  }

  q = xQueueCreate(slots + 2, sizeof(int));
  written = 0;
  bytes = 0;
  totalBytes = 0;
  overrunN = 0;
  markN = 0;
  histN = 0;
  memset(hist, 0, sizeof hist);
  blockLen = rate / 32;
  blockCnt = 0;
  blockPeak = 0;
  paused = false;
  writerDone = false;
  capRun = true;
  st = RECORDING;
  xTaskCreatePinnedToCore(writerTask, "recw", 6144, nullptr, 4, nullptr, 0);
  xTaskCreatePinnedToCore(captureTask, "recc", 4096, nullptr, 5, nullptr, 0);
  return true;
}

void stop() {
  if (st != RECORDING && st != PAUSED) return;
  capRun = false;
  st = STOPPING;
}

void togglePause() {
  if (st == RECORDING) {
    paused = true;
    st = PAUSED;
  } else if (st == PAUSED) {
    paused = false;
    st = RECORDING;
  }
}

void mark() {
  if ((st == RECORDING || st == PAUSED) && markN < 64) marks[markN++] = elapsedMs() / 1000.0f;
}

void service() {
  if (st == STOPPING && writerDone) {
    micClose();
    heap_caps_free(stereo);
    stereo = nullptr;
    M5.Speaker.begin();
    M5.Speaker.setVolume(S.volume);
    vQueueDelete(q);
    q = nullptr;
    heap_caps_free(ring);
    ring = nullptr;
    finished = true;
    snprintf(finishedPath, sizeof finishedPath, "/REC/%s.wav", baseName);
    finishedMs = elapsedMs();
    st = IDLE;
  }
}

State state() { return st; }
uint32_t elapsedMs() { return rate ? (uint32_t)(totalBytes * 500 / rate) : 0; }
uint64_t dataBytes() { return totalBytes; }
uint32_t sampleRate() { return rate; }
const char* path() { return filePath; }
int presetIdx() { return presetI; }
uint16_t presetOffSec() { return kPresets[presetI].offSec; }
uint32_t overruns() { return overrunN; }
int markCount() { return markN; }
const char* lastError() { return err; }

bool takeFinished(char* out, size_t n, uint32_t* ms) {
  if (!finished) return false;
  finished = false;
  strlcpy(out, finishedPath, n);
  if (ms) *ms = finishedMs;
  return true;
}

int history(uint8_t* out, int n) {
  uint32_t total = histN;
  int have = std::min<uint32_t>(total, 512);
  int k = std::min(n, have);
  for (int i = 0; i < n - k; ++i) out[i] = 0;
  for (int i = 0; i < k; ++i) out[n - k + i] = hist[(total - k + i) % 512];
  return k;
}

float peakLevel() { return peakSm; }
uint32_t historyTotal() { return histN; }
uint8_t historyAt(uint32_t idx) {
  uint32_t total = histN;
  if (idx >= total || total - idx > 500) return 0;
  return hist[idx % 512];
}
float historyRate() { return blockLen ? (float)rate / blockLen : 32; }

int scope(int16_t* out, int n) {
  n = std::min(n, 128);
  memcpy(out, scopeBuf, n * 2);
  return n;
}

bool readWavInfo(File& f, WavInfo& w) {
  uint8_t h[12];
  f.seek(0);
  if (f.read(h, 12) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) return false;
  w = {16000, 1, 16, 44, 0};
  uint32_t pos = 12, size = f.size();
  bool gotFmt = false;
  while (pos + 8 <= size) {
    uint8_t c[8];
    f.seek(pos);
    if (f.read(c, 8) != 8) break;
    uint32_t len;
    memcpy(&len, c + 4, 4);
    if (!memcmp(c, "fmt ", 4)) {
      uint8_t fm[16];
      f.read(fm, 16);
      memcpy(&w.channels, fm + 2, 2);
      memcpy(&w.rate, fm + 4, 4);
      memcpy(&w.bits, fm + 14, 2);
      gotFmt = true;
    } else if (!memcmp(c, "data", 4)) {
      w.dataOffset = pos + 8;
      // an unfinished header (power loss) reports 0: trust the file size instead
      uint32_t avail = size - w.dataOffset;
      w.dataBytes = (len == 0 || len > avail) ? avail : len;
      return gotFmt && w.bits == 16;
    }
    pos += 8 + len + (len & 1);
  }
  return false;
}

}  // namespace rec
