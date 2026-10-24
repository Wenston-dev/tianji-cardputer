// Kindle page turner: a BLE HID remote the Kindle accepts as a "Kindle Click"
// (Amazon VID 0x0171 / PID 0x0443 in the advert and the DIS PnP ID).
// Protocol notes and the NimBLE structure follow zuyan9/Open-Kindle-Click-ESP32-C6 (MIT).
#pragma once
#include <Arduino.h>

namespace kclick {

enum State { OFF, PAIRING, WAITING, CONNECTED, READY };
enum Key { NEXT, PREV, BRI_UP, BRI_DOWN, SLEEP, KEY_COUNT };

bool begin();  // start the BLE stack and advertise; false if it was disabled at boot (restart needed)
void end();    // stop it completely (saves power)
bool on();
State state();
bool bonded();
void forget();      // drop every bond; also Forget the remote on the Kindle
void wake();        // advertise fast again (e.g. when the page-turner screen opens)
void service();     // call every loop
bool tap(Key k);    // false if not ready (page turns are queued for a few seconds)
uint32_t pages();   // page turns delivered this session

}  // namespace kclick
