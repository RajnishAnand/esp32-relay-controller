#pragma once

#include <Arduino.h>
#include <time.h>

namespace platform {

class Mutex {
 public:
  Mutex() : handle_(nullptr) {}
  void init();
  void lock();
  void unlock();

 private:
  void* handle_;
};

class Guard {
 public:
  explicit Guard(Mutex& mutex) : mutex_(mutex) { mutex_.lock(); }
  ~Guard() { mutex_.unlock(); }

 private:
  Guard(const Guard&);
  Guard& operator=(const Guard&);
  Mutex& mutex_;
};

void beginWatchdog();
void feed();
void restart();
time_t epochNow();
uint32_t freeHeap();
uint32_t minFreeHeap();
uint32_t maxAllocHeap();
const char* resetReason();
bool elapsed(unsigned long since, unsigned long durationMs);
bool startTask(const char* name, void (*fn)(void*), void* arg, uint32_t stackBytes);
void sleepMs(uint32_t ms);
void noteLoopDuration(unsigned long ms);
unsigned long loopMaxMs();

}  // namespace platform
