#!/bin/bash
# Build the host preview: the real UI code compiled for macOS against M5GFX + SDL2 (`brew install sdl2`),
# rendering frames to image files instead of a screen.  ./build.sh [all]  then  ./preview out [scene]
#   scenes: home liuren qian jiao dice coin rec lib set usb pomo kindle   (python3 sheet.py out -> out/sheet.png)
set -e
cd "$(dirname "$0")"
# M5GFX source tree (the Arduino library); override with M5GFX_SRC
G=${M5GFX_SRC:-$HOME/Documents/Arduino/libraries/M5GFX/src}
SDK=$(xcrun --show-sdk-path)
T=../../tianji
mkdir -p obj out
python3 ../mkfont.py $T > /dev/null 2>&1 || echo "note: using committed fontdata.cpp"
GFXSRC=$(ls $G/lgfx/v1/*.cpp $G/lgfx/v1/misc/*.cpp $G/lgfx/v1/platforms/sdl/*.cpp $G/lgfx/v1/panel/Panel_Device.cpp $G/lgfx/v1/panel/Panel_FrameBufferBase.cpp 2>/dev/null || true)
CSRC=$(find $G/lgfx/Fonts $G/lgfx/utility -name "*.c")
CXXFLAGS="-std=c++17 -O1 -g -w -isysroot $SDK -isystem $SDK/usr/include/c++/v1 -I host -I $T -I $G -I /opt/homebrew/include -I /opt/homebrew/include/SDL2"
# M5GFX objects are cached
for s in $GFXSRC; do
  o=obj/$(echo $s | md5 -q).o
  [ -f $o ] || clang++ $CXXFLAGS -x c++ -c $s -o $o
done
for s in $CSRC; do
  o=obj/$(echo $s | md5 -q).o
  [ -f $o ] || clang -O1 -w -I $G -isysroot $SDK -c $s -o $o
done
APPS="$T/gfx.cpp $T/r3d.cpp $T/lunar.cpp $T/launch.cpp $T/fontdata.cpp $T/app_home.cpp $T/app_liuren.cpp"
DEF=""
if [ "$1" == "all" ]; then
  APPS="$APPS $(ls $T/app_qian.cpp $T/app_jiao.cpp $T/app_dice.cpp $T/app_coin.cpp $T/app_recorder.cpp $T/app_library.cpp $T/app_settings.cpp $T/app_usb.cpp $T/app_pomo.cpp $T/app_kindle.cpp $T/data_qian.cpp 2>/dev/null) host_store.cpp"
  DEF="-DHAVE_ALL"
fi
clang++ $CXXFLAGS $DEF $APPS host_stubs.cpp main.cpp obj/*.o -L/opt/homebrew/lib -lSDL2 -o preview
echo built
