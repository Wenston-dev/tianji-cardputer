#!/bin/bash
# Build (and optionally flash) the 天机 firmware.
#   ./build.sh                 compile into build/
#   ./build.sh upload <port>   compile and flash, e.g. /dev/cu.usbmodem1101
# The glyph table (tianji/fontdata.cpp) is regenerated only when Source Han Sans is available;
# otherwise the committed copy is used. See tools/mkfont.py.
set -e
cd "$(dirname "$0")"
FQBN="m5stack:esp32:m5stack_cardputer:FlashSize=8M,PartitionScheme=custom,USBMode=default,CDCOnBoot=default"
if python3 tools/mkfont.py tianji 2>/dev/null; then :; else echo "note: font not found, using tianji/fontdata.cpp as committed"; fi
arduino-cli compile --fqbn "$FQBN" --output-dir build tianji
if [ "$1" == "upload" ]; then
  arduino-cli upload --fqbn "$FQBN" --input-dir build -p "${2:?serial port}" tianji
fi
