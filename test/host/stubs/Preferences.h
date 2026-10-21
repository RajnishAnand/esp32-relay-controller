#pragma once
#include "Arduino.h"

extern std::map<std::string, std::string> g_nvs;

class Preferences {
 public:
  bool begin(const char*, bool = false);
  bool getBool(const char*, bool = false);
  size_t putBool(const char*, bool);
  uint8_t getUChar(const char*, uint8_t = 0);
  size_t putUChar(const char*, uint8_t);
  uint32_t getULong(const char*, uint32_t = 0);
  size_t putULong(const char*, uint32_t);
  String getString(const char*, String = String());
  size_t putString(const char*, String);
  bool remove(const char*);
};
