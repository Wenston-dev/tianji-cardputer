# Engineering notes — Cardputer ADV

Things learned while building TianJi on a real Cardputer ADV (ESP32-S3, 8 MB flash, `m5stack:esp32` core 3.3.7 = arduino-esp32 on ESP-IDF 5.5.x; M5Cardputer 1.1.1, M5Unified 0.2.17, M5GFX 0.2.22). Everything below was measured or tried on the device unless marked otherwise.

## Hardware map (as used in this firmware)

| Part | Detail |
|---|---|
| Display | 240×135, driven by M5GFX; the UI draws into a 16-bit off-screen canvas and pushes it each frame |
| SD card | SPI: SCK 40, MISO 39, MOSI 14, CS 12 (`SD.begin(12, SPI, 25000000)`) |
| Audio codec | ES8311 at I²C 0x18 on the internal I²C bus; I²S: BCLK 41, WS 43, DOUT 42 (speaker), DIN 46 (mic) |
| Keyboard | TCA8418 matrix controller (via M5Cardputer); modifier keys are Fn / Shift / Ctrl / Opt / Alt |
| IMU | BMI270 (shake detection = change of the acceleration vector relative to its resting magnitude) |
| Button | G0 (BtnA) |

## Recording: `M5.Mic` returns a constant on arduino-esp32 3.3.x

**Symptom:** WAV files full of one repeated value (−8, −1…), no error anywhere.
Several people hit the same thing ([esp-idf#18621](https://github.com/espressif/esp-idf/issues/18621), [cardputer-claude-os#8](https://github.com/dakshaymehta/cardputer-claude-os/issues/8)); one common workaround is pinning the core to 3.2.0 ([audio-spy](https://github.com/Tombotronic/audio-spy)).

**What we measured** (`tools/mictest`, results written to flash and read back):

| Strategy | Result |
|---|---|
| `M5.Mic.begin()` / `record()` | constant |
| `M5.Mic` after resetting the I²S pins | constant |
| Own `i2s_std` RX (Philips, 16-bit stereo, 16 kHz) + ES8311 programmed over I²C, before any speaker use | **real audio** |
| Same, *after* `M5.Speaker` had been used | constant on `I2S_NUM_0` without a pin reset; **real audio** with `gpio_reset_pin(41/42/43/46)` first, or on `I2S_NUM_1` |

So TianJi (`tianji/rec.cpp`) skips `M5.Mic` entirely: it ends the speaker, frees the four pins, programs the codec and runs its own RX channel on `I2S_NUM_1`, and frees the pins again afterwards. The ES8311 setup:

```
0x00=0x80  0x01=0xBA (MCLK from BCLK)  0x02=0x18  0x09=0x0C  0x0A=0x0C
0x0D=0x01  0x0E=0x0A (PGA + ADC on)    0x14=0x10|pga (3 dB steps, 0..10)
0x16=0x04  0x17=0xBF (0 dB)            0x1C=0x6A
```

Both stereo slots carry the microphone; the left one is kept. The codec is programmed from the main thread, never from the capture task, so it can't collide with keyboard / IMU polling on the same I²C bus.

## Getting back into the app after flashing

In my tests, a reset through the RTS line (what `arduino-cli upload` ends with, and what opening the serial port from a PC does) left this board in the ROM **download mode** — black screen, no app. A watchdog reset (`esptool --after watchdog-reset`) or the reset button starts the app normally.

## USB drive mode (SD card as a USB disk)

The board has one USB PHY, which normally belongs to USB-Serial/JTAG (that is what makes flashing work). `tianji/usbdrive.cpp`:

- detects "a computer is attached" by watching the SOF frame counter of the JTAG peripheral tick (a plain charger doesn't);
- switches the PHY to TinyUSB and exposes the SD card with `USBMSC` (`SD.readRAW` / `writeRAW`);
- leaves by giving the PHY back to JTAG (the same register sequence as the core's `usb_switch_to_cdc_jtag()`) and `esp_restart()`;
- keeps a magic value in `RTC_NOINIT` across that restart so the device doesn't immediately offer the drive again while the cable is still plugged in; a reset caused by the flashing tool is treated the same way.

## Bluetooth and RAM

- The arduino-esp32 core releases the Bluetooth controller's memory (~36 KB) at boot unless `btInUse()` says it's needed. TianJi overrides `btInUse()` to read the "Bluetooth page turner" setting from NVS, so turning the feature off gives that RAM to everything else (it applies after a reboot).
- The NimBLE host + controller want ~65 KB. With the speaker, sound buffers and the WiFi stack resident, the largest free block dropped to **176 bytes** and nothing worked. So Bluetooth runs **only while the page-turner screen is open**: entering it ends the speaker, frees the sound buffers and aborts a running WiFi sync; leaving it hands everything back (about 17 KB of heap left while inside the screen, ~75 KB after leaving).
- `ble_hs_cfg`: Just Works, bonding, legacy pairing; the advertising payload must contain Flags `0x06`, then manufacturer data `71 01 04 43` as the *first* `0xFF` AD, then the HID service UUID. See the notes in `tianji/kclick.cpp` and the protocol write-up in [Open-Kindle-Click-nRF52840/PROTOCOL.md](https://github.com/zuyan9/Open-Kindle-Click-nRF52840/blob/main/PROTOCOL.md).

## Standby instead of power-off (no RTC on this board)

The Cardputer ADV has no battery-backed RTC, so cutting power stops the clock; TianJi saves the time to NVS every minute and restores it on the next boot (marked untrusted, with a home-screen hint and a picker on **T**). For everyday use there is **standby** (`tianji/standby.cpp`, hold G0 for 1.5 s): the ESP32-S3 deep-sleeps and its RTC timer keeps counting.

- Before sleeping: a recording is stopped and finalised, the clock is saved, WiFi/Bluetooth/speaker are shut down, the ES8311 analog side is powered down, the IMU is put to sleep, the SD card is unmounted, the panel gets `SLPIN`, and the backlight pin (GPIO38) is driven low and held with `gpio_hold_en` + `gpio_deep_sleep_hold_en` (it floats otherwise). `standbyBootFixup()` releases that hold first thing on the next boot.
- Wake sources: **G0** (GPIO0) and **any key** — the TCA8418 keyboard controller pulls its INT line (GPIO11) low on a key event. Both are armed with `esp_sleep_enable_ext1_wakeup(…, ESP_EXT1_WAKEUP_ANY_LOW)` with RTC pull-ups (RTC peripherals stay powered for them). The keyboard's event FIFO is drained over I²C (addr 0x34: read 0x03 for the count, 0x04 per event, clear 0x02) so INT is high when the chip goes to sleep; if it still reads low, only G0 is armed. On wake both pins are `rtc_gpio_deinit`-ed back to the digital matrix.
- Whether the clock can be trusted is kept in `RTC_NOINIT` memory, so it survives deep sleep and `esp_restart()` but not a power loss. The automatic WiFi sync runs only when the clock is **not** trusted (i.e. after a real power-off); waking from standby never starts WiFi, which would otherwise hold RAM the recorder needs. Starting a recording also aborts any sync in progress.
- The deep-sleep clock runs on the chip's internal RC oscillator, not a crystal. Measured with a `-DSLEEPTEST` build (NTP sync, 300 s timer sleep, sync again): it ran **≈1.15 % fast** (301.0 s counted for 297.6 s asleep, ≈16.5 min/day). `standbyCorrectClock()` scales the time spent asleep back by that rate error (`DEFAULT_PPM = 11450`). With the correction the same test came out 0.08 s off after 298 s (≈280 ppm, ≈24 s/day slow), which is close to what one NTP round trip can resolve. Each NTP sync that follows a standby of at least 10 minutes moves the stored rate (NVS `rtcppm`) halfway towards the measured one, so it adapts to the particular board and temperature.
- Waking is a full boot (about a second).

## Performance

`tools/perf.sh` flashes a `-DPERFTEST` build that visits each 3D scene, throws, and stores per-frame timings in the spare flash partition. Pushing a frame costs a fixed **12.5–13 ms** (SPI), so only the drawing side can improve. Average draw time per frame:

| Scene | before | after |
|---|---|---|
| Dice ×6, idle | 16.6 ms | 11.0 ms |
| Dice, being thrown | 68.6 ms (worst 437 ms) | 12.7 ms (worst 18 ms) |
| Coin, flipping | 40.3 ms (worst 436 ms) | 12.8 ms (worst 21 ms) |
| 掷筊, flying | 19.6 ms (worst 298 ms) | 9.4 ms (worst 16 ms) |
| Fortune tube, idle / shaking | 24.5 / 83.9 ms | 10.4 / 11.1 ms |

What mattered, in order: sound synthesis (`sinf` + `expf` per sample made a long ring take hundreds of ms — now recurrences), lighting (`powf` removed, one shading result per flat face), glow / shadow loops that only visit pixels inside the circle, the coin's per-pixel shader (light and half-vector rotated into coin space once per frame), and 22 anti-aliased wedge lines replaced by two triangles each.

## Lunar calendar

`tianji/lunar_table.inc` (month lengths and leap months, 1900–2100) is generated from ICU's Chinese calendar (`node tools/gen_lunar.js`) because a hand-typed table had errors in several years; `node tools/verify_lunar.js` checks the table *and* the conversion code against ICU for all 72,999 days from 1901-02-19 to 2100-12-30.

## Handy facts

- Partition table (`tianji/partitions.csv`): 6 MB app, ~1.9 MB `spiffs` partition. TianJi doesn't use it for files; the test builds (`-DSELFTEST`, `-DPERFTEST`, `-DSLEEPTEST`) write their reports there, and `esptool read_flash 0x610000 …` reads them back without a serial connection.
- Settings live in NVS (namespace `tianji`), pomodoro presets in namespace `pomo`.
- SD layout: `/REC/YYYYMMDD_HHMMSS.wav` (+ `.txt` with marker times), `/POMO/focus.csv` (`start_unix,seconds,completed,preset`).
