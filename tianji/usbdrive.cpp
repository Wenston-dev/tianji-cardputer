#include "usbdrive.h"
#include <SD.h>
#include "USB.h"
#include "USBMSC.h"
#include "driver/periph_ctrl.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/usb_pins.h"
#include "soc/usb_serial_jtag_reg.h"
#include "tusb.h"
#include "rec.h"

namespace usbdrive {

static USBMSC msc;
static bool on = false;
static volatile bool wasEjected = false;
static volatile uint32_t nRead = 0, nWritten = 0;

// Survives esp_restart(): set when we leave drive mode so the reboot doesn't jump straight back in.
RTC_NOINIT_ATTR static uint32_t suppressMagic;
static constexpr uint32_t SUPPRESS = 0x75736221;
static bool suppressed = false, bootChecked = false;

static uint32_t lastFrame = 0, lastPoll = 0;
static uint8_t ticking = 0;
static bool present = false, prevPresent = false;

bool hostPresent() {
  if (on) return true;
  uint32_t now = millis();
  if (now - lastPoll >= 50) {
    lastPoll = now;
    uint32_t f = REG_READ(USB_SERIAL_JTAG_FRAM_NUM_REG) & USB_SERIAL_JTAG_SOF_FRAME_INDEX;
    // frames arrive every 1 ms while a host is attached
    if (f != lastFrame) ticking = std::min(ticking + 1, 10);
    else ticking = ticking > 2 ? ticking - 2 : 0;
    lastFrame = f;
    present = ticking >= 6;
  }
  return present;
}

bool hostArrived() {
  static uint32_t absentSince = 0;
  if (!bootChecked) {
    bootChecked = true;
    // after leaving drive mode, or a reset from the flashing tool, wait for a fresh plug-in
    esp_reset_reason_t why = esp_reset_reason();
    suppressed = suppressMagic == SUPPRESS || why == ESP_RST_USB || why == ESP_RST_WDT || why == ESP_RST_DEEPSLEEP;
    suppressMagic = 0;
    absentSince = 0;
  }
  bool p = hostPresent();
  bool edge = p && !prevPresent;
  prevPresent = p;
  // only an unplug lasting a while re-arms it (presence detection needs a moment after boot)
  if (p) absentSince = 0;
  else if (!absentSince) absentSince = millis();
  else if (millis() - absentSince > 2000) suppressed = false;
  return edge && !suppressed;
}

static int32_t onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
  uint8_t* b = (uint8_t*)buffer;
  for (uint32_t i = 0; i < bufsize / 512; ++i)
    if (!SD.readRAW(b + i * 512, lba + i)) return -1;
  nRead += bufsize;
  return bufsize;
}

static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
  for (uint32_t i = 0; i < bufsize / 512; ++i)
    if (!SD.writeRAW(buffer + i * 512, lba + i)) return -1;
  nWritten += bufsize;
  return bufsize;
}

static bool onStartStop(uint8_t, bool start, bool loadEject) {
  if (loadEject && !start) wasEjected = true;
  return true;
}

bool begin() {
  if (on) return true;
  if (rec::state() != rec::IDLE || !rec::sdBegin()) return false;
  msc.vendorID("TianJi");
  msc.productID("Cardputer SD");
  msc.productRevision("1.0");
  msc.onRead(onRead);
  msc.onWrite(onWrite);
  msc.onStartStop(onStartStop);
  msc.mediaPresent(true);
  if (!msc.begin(SD.numSectors(), SD.sectorSize())) return false;
  USB.productName("TianJi Recorder");
  USB.begin();
  on = true;
  wasEjected = false;
  return true;
}

bool active() { return on; }
bool mounted() { return on && tud_mounted(); }
bool ejected() { return wasEjected; }
bool suspended() { return on && tud_suspended(); }
uint32_t bytesRead() { return nRead; }
uint32_t bytesWritten() { return nWritten; }

// Mirrors the core's usb_switch_to_cdc_jtag(): give the PHY back to USB-Serial/JTAG, then reboot.
void exitAndRestart() {
  if (on) {
    if (tusb_inited()) {
      tud_disconnect();
      delay(30);
    }
    periph_module_reset(PERIPH_USB_MODULE);
    periph_module_disable(PERIPH_USB_MODULE);
    CLEAR_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG, (RTC_CNTL_SW_HW_USB_PHY_SEL | RTC_CNTL_SW_USB_PHY_SEL | RTC_CNTL_USB_PAD_ENABLE));
    CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_PHY_SEL);
    CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
    pinMode(USBPHY_DM_NUM, OUTPUT_OPEN_DRAIN);
    pinMode(USBPHY_DP_NUM, OUTPUT_OPEN_DRAIN);
    digitalWrite(USBPHY_DM_NUM, LOW);
    digitalWrite(USBPHY_DP_NUM, LOW);
    delay(20);
    CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_PAD_PULL_OVERRIDE);
    SET_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_DP_PULLUP);
    SET_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
    SET_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG, RTC_CNTL_SW_HW_USB_PHY_SEL);
    CLEAR_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG, RTC_CNTL_SW_USB_PHY_SEL);
    delay(80);
  }
  suppressMagic = SUPPRESS;
  esp_restart();
}

}  // namespace usbdrive
