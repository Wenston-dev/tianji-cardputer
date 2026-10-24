#include "lunar.h"
#include "system.h"
#include "lunar_table.inc"

static int leapMonth(int y) { return kLunarInfo[y - 1900] & 0xF; }
static int leapDays(int y) { return leapMonth(y) ? ((kLunarInfo[y - 1900] & 0x10000) ? 30 : 29) : 0; }
static int monthDays(int y, int m) { return (kLunarInfo[y - 1900] & (0x10000 >> m)) ? 30 : 29; }
static int yearDays(int y) {
  int s = 348;
  for (uint32_t i = 0x8000; i > 0x8; i >>= 1) s += (kLunarInfo[y - 1900] & i) ? 1 : 0;
  return s + leapDays(y);
}

bool toLunar(int Y, int M, int D, LunarDate& out) {
  long off = daysFromCivil(Y, M, D) - daysFromCivil(1900, 1, 31);
  if (off < 0 || Y > 2100) return false;
  int y = 1900, t = 0;
  for (; y < 2101 && off > 0; ++y) {
    t = yearDays(y);
    off -= t;
  }
  if (off < 0) {
    off += t;
    --y;
  }
  int lp = leapMonth(y);
  bool isLeap = false;
  int m = 1;
  for (; m < 13 && off > 0; ++m) {
    if (lp > 0 && m == lp + 1 && !isLeap) {
      --m;
      isLeap = true;
      t = leapDays(y);
    } else {
      t = monthDays(y, m);
    }
    if (isLeap && m == lp + 1) isLeap = false;
    off -= t;
  }
  if (off == 0 && lp > 0 && m == lp + 1) {
    if (isLeap) isLeap = false;
    else {
      isLeap = true;
      --m;
    }
  }
  if (off < 0) {
    off += t;
    --m;
  }
  out = {y, m, (int)off + 1, isLeap};
  return true;
}

const char* lunarMonthName(int m) {
  static const char* n[] = {"正月", "二月", "三月", "四月", "五月", "六月", "七月", "八月", "九月", "十月", "冬月", "腊月"};
  return n[(m - 1) % 12];
}

void lunarDayName(int d, char* out) {
  static const char* a[] = {"初", "十", "廿", "三"};
  static const char* b[] = {"十", "一", "二", "三", "四", "五", "六", "七", "八", "九"};
  if (d == 10) strcpy(out, "初十");
  else if (d == 20) strcpy(out, "二十");
  else if (d == 30) strcpy(out, "三十");
  else {
    strcpy(out, a[d / 10]);
    strcat(out, b[d % 10]);
  }
}

void ganzhiYear(int y, char* out) {
  static const char* g[] = {"甲", "乙", "丙", "丁", "戊", "己", "庚", "辛", "壬", "癸"};
  static const char* z[] = {"子", "丑", "寅", "卯", "辰", "巳", "午", "未", "申", "酉", "戌", "亥"};
  int k = ((y - 4) % 60 + 60) % 60;
  strcpy(out, g[k % 10]);
  strcat(out, z[k % 12]);
}

int shichenIndex(int hour) { return ((hour + 1) / 2) % 12; }

const char* shichenName(int idx) {
  static const char* z[] = {"子", "丑", "寅", "卯", "辰", "巳", "午", "未", "申", "酉", "戌", "亥"};
  return z[idx % 12];
}
