// Gregorian -> Chinese lunar calendar (1900-2100), names for months, days and double-hours.
#pragma once
#include <Arduino.h>

struct LunarDate {
  int year, month, day;
  bool leap;
};

bool toLunar(int y, int m, int d, LunarDate& out);
const char* lunarMonthName(int m);  // 正月 .. 腊月
void lunarDayName(int d, char* out);  // 初一 .. 三十
void ganzhiYear(int y, char* out);    // 丙午
int shichenIndex(int hour);           // 0 = 子 .. 11 = 亥
const char* shichenName(int idx);     // 子 .. 亥
