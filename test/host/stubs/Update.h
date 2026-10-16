#pragma once
#include "Arduino.h"

class UpdateClass {
 public:
  bool begin(size_t = 0, int = 0);
  size_t write(uint8_t*, size_t);
  bool end(bool = false);
  void abort();
};
extern UpdateClass Update;
extern std::string g_updateWritten;
extern bool g_updateEnded;
extern bool g_updateAborted;
