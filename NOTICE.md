# Third-party notices

TianJi itself is MIT-licensed (see `LICENSE`). It contains or builds on the following.

## Code adapted into this repository

**Open-Kindle-Click-ESP32-C6** — https://github.com/zuyan9/Open-Kindle-Click-ESP32-C6 — MIT, Copyright (c) 2026 zuyan9.
`tianji/kclick.cpp` (the Kindle page turner) is adapted from it: advertising payload, GATT layout, HID report map and the link-readiness logic. Full license text: `licenses/MIT-Open-Kindle-Click-ESP32-C6.txt`.
The behaviour the Kindle expects is documented in https://github.com/zuyan9/Open-Kindle-Click-nRF52840 (`PROTOCOL.md`) and implemented for the Flipper Zero in https://github.com/zuyan9/Flipper-Zero-Kindle-Click (both MIT).

## Font

**Source Han Sans (思源黑体)** — Copyright 2014-2025 Adobe, with Reserved Font Name "Source". SIL Open Font License 1.1: `licenses/OFL-SourceHanSans.txt`.
`tianji/fontdata.cpp` is a generated file holding bitmap renderings of a few hundred glyphs of this font (see `tools/mkfont.py`); it is not the font software and is not distributed under the name "Source".

## Libraries used at build time (not included in this repository)

| Library | License |
|---|---|
| [M5Cardputer](https://github.com/m5stack/M5Cardputer), [M5Unified](https://github.com/m5stack/M5Unified), [M5GFX](https://github.com/m5stack/M5GFX) | MIT |
| [arduino-esp32](https://github.com/espressif/arduino-esp32) (via the `m5stack:esp32` core) | LGPL-2.1 |
| [ESP-IDF](https://github.com/espressif/esp-idf), including the NimBLE host stack | Apache-2.0 |

A prebuilt firmware image links these; their licenses apply to the image as well.

## Ideas and information only (no source files copied)

- [Wenston-dev/omarchy-memento](https://github.com/Wenston-dev/omarchy-memento) (MIT) — the "remaining life" countdown idea and the life-in-a-grid presentation.
- [KyleBing/m5stack-cardputer-sparks](https://github.com/KyleBing/m5stack-cardputer-sparks) (no license declared) — single-letter app shortcuts, and how the single USB PHY can be switched between USB-Serial/JTAG and TinyUSB. TianJi's `usbdrive.cpp` was written independently from the Arduino core's `usb_switch_to_cdc_jtag()` and this idea; no file was copied.
- [Cardenza-Community/cardputer-studio](https://github.com/Cardenza-Community/cardputer-studio) (MIT) — ES8311 microphone register settings.
- [Tombotronic/audio-spy](https://github.com/Tombotronic/audio-spy) (MIT), [espressif/esp-idf#18621](https://github.com/espressif/esp-idf/issues/18621), [dakshaymehta/cardputer-claude-os#8](https://github.com/dakshaymehta/cardputer-claude-os/issues/8) — diagnosis hints for the Cardputer ADV microphone returning constant samples on arduino-esp32 3.3.x.

## Data

- The lunar calendar table (`tianji/lunar_table.inc`) is generated from ICU's Chinese calendar by `tools/gen_lunar.js` and verified against it for every day 1901–2100 by `tools/verify_lunar.js`.
- The fortune-stick verses are couplets from classical Chinese poems (Li Bai, Du Fu, Su Shi, Lu You and others), which are in the public domain. The readings are original text.
