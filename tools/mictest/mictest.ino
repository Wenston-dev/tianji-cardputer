// Microphone probe for Cardputer ADV. Runs several capture strategies one after another and
// stores a text report in the spiffs partition, which is read back with esptool read_flash.
#include <M5Cardputer.h>
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_partition.h"

static char report[2048];
static int rlen = 0;
#define LOG(...) (rlen += snprintf(report + rlen, sizeof(report) - rlen, __VA_ARGS__))

static int16_t buf[4096];

static void stats(const char* name, int n) {
  int mn = 32767, mx = -32768, distinct = 0, prev = 1 << 20;
  long sum = 0;
  for (int i = 0; i < n; ++i) {
    int v = buf[i];
    mn = std::min(mn, v), mx = std::max(mx, v), sum += v;
    if (v != prev) ++distinct;
    prev = v;
  }
  LOG("%s n=%d min=%d max=%d mean=%ld changes=%d\n", name, n, mn, mx, n ? sum / n : 0, distinct);
}

static void es(uint8_t r, uint8_t v) { M5.In_I2C.writeRegister8(0x18, r, v, 400000); }
static void esMicOn() {
  es(0x00, 0x80); es(0x01, 0xBA); es(0x02, 0x18); es(0x09, 0x0C); es(0x0A, 0x0C);
  es(0x0D, 0x01); es(0x0E, 0x0A); es(0x14, 0x1A); es(0x16, 0x04); es(0x17, 0xBF); es(0x1C, 0x6A);
}

static void m5mic(const char* name) {
  M5.Mic.begin();
  for (int k = 0; k < 3; ++k) {  // first rounds include the restart
    M5.Mic.record(buf, 1600, 16000);
    while (M5.Mic.isRecording()) delay(1);
  }
  stats(name, 1600);
  M5.Mic.end();
}

static void ownI2s(const char* name, i2s_port_t port, bool philips) {
  esMicOn();
  i2s_chan_handle_t rx = nullptr;
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(port, I2S_ROLE_MASTER);
  cc.dma_desc_num = 8;
  cc.dma_frame_num = 256;
  esp_err_t e1 = i2s_new_channel(&cc, nullptr, &rx);
  i2s_std_config_t sc = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
      .slot_cfg = philips ? (i2s_std_slot_config_t)I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)
                          : (i2s_std_slot_config_t)I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
      .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = GPIO_NUM_41, .ws = GPIO_NUM_43, .dout = I2S_GPIO_UNUSED,
                   .din = GPIO_NUM_46, .invert_flags = {false, false, false}}};
  esp_err_t e2 = rx ? i2s_channel_init_std_mode(rx, &sc) : -1;
  esp_err_t e3 = rx ? i2s_channel_enable(rx) : -1;
  LOG("%s err %d %d %d\n", name, e1, e2, e3);
  if (rx && e3 == ESP_OK) {
    size_t br = 0;
    for (int k = 0; k < 4; ++k) i2s_channel_read(rx, buf, sizeof buf, &br, 500);
    i2s_channel_read(rx, buf, 1600 * 4, &br, 1000);
    int n = br / 4;
    static int16_t l[1600], r[1600];
    for (int i = 0; i < n; ++i) l[i] = buf[i * 2], r[i] = buf[i * 2 + 1];
    memcpy(buf, l, n * 2);
    char nm[32];
    snprintf(nm, sizeof nm, "%s L", name);
    stats(nm, n);
    memcpy(buf, r, n * 2);
    snprintf(nm, sizeof nm, "%s R", name);
    stats(nm, n);
    i2s_channel_disable(rx);
  }
  if (rx) i2s_del_channel(rx);
}

static void speakerRound() {
  M5.Speaker.begin();
  M5.Speaker.setVolume(80);
  M5.Speaker.tone(1200, 150);
  delay(250);
  M5.Speaker.end();
}

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  M5.Display.fillScreen(TFT_NAVY);
  M5.Display.setTextSize(2);
  M5.Display.drawString("mic probe 2...", 10, 10);
  LOG("MICPROBE2 idf=%s\n", esp_get_idf_version());
  speakerRound();
  ownI2s("F spk->own0", I2S_NUM_0, true);
  speakerRound();
  for (int p : {41, 42, 43, 46}) gpio_reset_pin((gpio_num_t)p);
  ownI2s("G spk->reset->own0", I2S_NUM_0, true);
  speakerRound();
  ownI2s("H spk->own1", I2S_NUM_1, true);
  speakerRound();
  for (int p : {41, 42, 43, 46}) gpio_reset_pin((gpio_num_t)p);
  ownI2s("I spk->reset->own1", I2S_NUM_1, true);
  LOG("END\n");
  const esp_partition_t* p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "spiffs");
  if (p) {
    esp_partition_erase_range(p, 0, 4096);
    esp_partition_write(p, 0, report, 2048);
  }
  M5.Display.fillScreen(TFT_DARKGREEN);
  M5.Display.drawString("probe done", 10, 10);
}

void loop() { delay(100); }
