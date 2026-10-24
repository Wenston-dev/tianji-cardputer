// USB mass-storage mode: hand the SD card to a computer as a USB drive.
// The board boots on USB-Serial/JTAG (so flashing keeps working); entering drive mode switches the
// single USB PHY to TinyUSB, and leaving it restores JTAG and restarts so the card is remounted.
#pragma once
#include <Arduino.h>

namespace usbdrive {

// True while a computer (not just a charger) is talking to the JTAG port: its SOF counter ticks.
bool hostPresent();
// Polls hostPresent() and reports the moment a computer is plugged in.
bool hostArrived();

bool begin();       // switch to drive mode
bool active();
bool mounted();     // the computer has enumerated the drive
bool ejected();     // the computer ejected it
bool suspended();   // bus idle: cable pulled or host asleep
uint32_t bytesRead();
uint32_t bytesWritten();
void exitAndRestart();

}  // namespace usbdrive
