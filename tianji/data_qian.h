// The fortune slips: each verse is a couplet from classical poetry with a short reading.
#pragma once
#include <Arduino.h>

struct Lot {
  uint8_t level;  // 0 上上 .. 4 下下
  const char* l1;
  const char* l2;
  const char* src;  // author · work
  const char* jie;  // the reading
};

extern const Lot kLots[];
extern const int kLotCount;
extern const char* const kLevelName[5];
extern const char* const kAspectName[5];
extern const char* const kAspect[5][5];  // [level][aspect]
void lotNumber(int n, char* out);  // 1 -> 第一签
uint16_t levelColor(int level);
