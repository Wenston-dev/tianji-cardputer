#pragma once
#include "Arduino.h"
namespace fs { struct FS {}; }
struct File {
  operator bool() const { return false; }
  size_t size() { return 0; } bool seek(uint32_t) { return false; } int read(uint8_t*, size_t) { return 0; }
  void close() {} const char* name() { return ""; } bool isDirectory() { return false; }
  File openNextFile() { return File(); } time_t getLastWrite() { return 0; }
};
