#!/bin/bash
# Build, flash and read one mic probe variant. Usage: run.sh <variant> [fqbn-core]
set -e
cd "$(dirname "$0")"
V=$1
CORE=${2:-m5stack:esp32}
FQBN="$CORE:m5stack_cardputer:FlashSize=8M,USBMode=hwcdc,CDCOnBoot=cdc"
arduino-cli compile --fqbn "$FQBN" --build-property "compiler.cpp.extra_flags=-DV=$V" --output-dir build$V . 2>&1 | grep -E "error|Sketch uses" || true
P=$(ls /dev/cu.usbmodem* | head -1)
arduino-cli upload --fqbn "$FQBN" --input-dir build$V -p "$P" . 2>&1 | grep -E "rror|verified" | tail -1
sleep 2
python3 - "$P" <<'PY'
import serial, sys, time, glob
for _ in range(30):
    try:
        s = serial.Serial(glob.glob('/dev/cu.usbmodem*')[0], 115200, timeout=0.3); break
    except Exception: time.sleep(0.3)
t = time.time(); out = b''
while time.time() - t < 7:
    try: out += s.read(4096)
    except Exception: break
print(out.decode(errors='replace')[-900:])
PY
