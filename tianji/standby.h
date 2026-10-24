// Standby instead of power-off: the board has no battery-backed RTC, so a real power-off stops the
// clock. Deep sleep keeps the ESP32's RTC timer running; G0 or any key wakes it (a fresh boot).
#pragma once
#include <Arduino.h>

// Call early in setup(), before M5Cardputer.begin(): releases the pins held through deep sleep.
void standbyBootFixup();
// Saves state, shuts everything down and deep-sleeps. timerSec > 0 also wakes after that long.
// Does not return. Callers check for a running pomodoro first.
void standbyEnter(uint32_t timerSec = 0);
bool wokeFromStandby();
// Call after timeInit(): the deep-sleep clock runs on the chip's RC oscillator, about 1 % fast on
// this board, so the time spent asleep is scaled back by the learnt rate error.
void standbyCorrectClock();
// Call when an NTP sync succeeds: refines the rate error if we woke from standby this boot.
void standbyOnSync();
