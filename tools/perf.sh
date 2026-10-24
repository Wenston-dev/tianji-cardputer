#!/bin/bash
# Flash the PERFTEST build, let it run its script, and print the frame timings it stored.
set -e
cd "$(dirname "$0")/.."
FQBN="m5stack:esp32:m5stack_cardputer:FlashSize=8M,PartitionScheme=custom,USBMode=default,CDCOnBoot=default"
ET=$(ls -d ~/Library/Arduino15/packages/m5stack/tools/esptool_py/*/ | head -1)esptool
arduino-cli compile --fqbn "$FQBN" --build-property "compiler.cpp.extra_flags=-DPERFTEST" --output-dir build_perf tianji 2>&1 | grep -E "error|Sketch uses"
P=$(ls /dev/cu.usbmodem* | head -1)
arduino-cli upload --fqbn "$FQBN" --input-dir build_perf -p "$P" tianji 2>&1 | grep -E "rror|Wrote 2"
sleep 1
$ET --port "$P" --before no_reset --after watchdog-reset read_mac >/dev/null 2>&1 || true
sleep 40
OUT=$(mktemp)
$ET --port "$P" --after no_reset read_flash 0x610000 0x800 "$OUT" >/dev/null 2>&1
python3 -c "
d=open('$OUT','rb').read()
for i in range(14):
  c=d[i*128:(i+1)*128].split(b'\x00')[0]
  if c[:1]!=b'\xff' and c: print(c.decode().strip())"
