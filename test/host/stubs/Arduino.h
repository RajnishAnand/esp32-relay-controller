#pragma once

#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

#define PROGMEM
#define OUTPUT 1
#define HIGH 1
#define LOW 0

extern unsigned long g_millis;
inline unsigned long millis() { return g_millis; }
void delay(unsigned long ms);

struct PinWrite {
  unsigned long t;
  uint8_t pin;
  uint8_t level;
};
extern std::vector<PinWrite> g_pinWrites;
void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t level);
bool ledcSetup(uint8_t channel, uint32_t freq, uint8_t bits);
void ledcAttachPin(uint8_t pin, uint8_t channel);
void ledcWrite(uint8_t channel, uint32_t duty);
extern int g_configTimeCalls;
void configTime(long gmtOffsetSec, int dstOffsetSec, const char* server1, const char* server2 = nullptr,
                const char* server3 = nullptr);

class String {
 public:
  std::string s;
  String() {}
  String(const char* c) : s(c ? c : "") {}
  String(char c) : s(1, c) {}
  String(int v) : s(std::to_string(v)) {}
  String(unsigned int v) : s(std::to_string(v)) {}
  String(long v) : s(std::to_string(v)) {}
  String(unsigned long v) : s(std::to_string(v)) {}
  String& operator+=(const String& o) {
    s += o.s;
    return *this;
  }
  size_t length() const { return s.size(); }
  const char* c_str() const { return s.c_str(); }
  long toInt() const { return atol(s.c_str()); }
  int indexOf(char c, unsigned from = 0) const {
    size_t p = s.find(c, from);
    return p == std::string::npos ? -1 : (int)p;
  }
  String substring(unsigned a, unsigned b) const { return String(s.substr(a, b - a).c_str()); }
  String substring(unsigned a) const { return String(s.substr(a).c_str()); }
  bool startsWith(const String& p) const { return s.compare(0, p.s.size(), p.s) == 0; }
  void trim() {
    while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
    size_t i = 0;
    while (i < s.size() && isspace((unsigned char)s[i])) i++;
    s = s.substr(i);
  }
  bool equalsIgnoreCase(const String& o) const {
    if (s.size() != o.s.size()) return false;
    for (size_t i = 0; i < s.size(); i++)
      if (tolower((unsigned char)s[i]) != tolower((unsigned char)o.s[i])) return false;
    return true;
  }
  char operator[](unsigned i) const { return s[i]; }
  bool operator==(const String& o) const { return s == o.s; }
  bool operator!=(const String& o) const { return s != o.s; }
};
inline String operator+(const String& a, const String& b) {
  String r;
  r.s = a.s + b.s;
  return r;
}

struct SerialStub {
  void begin(int) {}
  void printf(const char*, ...) __attribute__((format(printf, 2, 3))) {}
};
extern SerialStub Serial;

struct EspStub {
  void restart();
  uint32_t getFreeHeap() { return 123456; }
  uint32_t getMinFreeHeap() { return 90000; }
  uint32_t getMaxAllocHeap() { return 60000; }
  uint64_t getEfuseMac() { return 0x112233445566ULL; }
};
extern EspStub ESP;
extern int g_restarts;

class IPAddress {
  uint8_t b[4];

 public:
  IPAddress() { memset(b, 0, 4); }
  IPAddress(uint8_t a, uint8_t c, uint8_t d, uint8_t e) {
    b[0] = a;
    b[1] = c;
    b[2] = d;
    b[3] = e;
  }
  uint8_t& operator[](int i) { return b[i]; }
  bool operator==(const IPAddress& o) const { return memcmp(b, o.b, 4) == 0; }
  bool operator!=(const IPAddress& o) const { return !(*this == o); }
  String toString() const {
    char buf[20];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    return String(buf);
  }
};
