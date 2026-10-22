#pragma once

#include <stdio.h>
#include <stdlib.h>

#include <iostream>
#include <string>
#include <vector>

#include <Arduino.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <Update.h>
#include <esp_ota_ops.h>

extern int g_failures;
extern time_t g_epoch;
extern int g_feeds;
extern std::string g_fixtureDir;
extern bool g_taskStartFails;
void runTasks();

struct TestCase {
  const char* name;
  void (*fn)();
};
std::vector<TestCase>& registry();
struct Registrar {
  Registrar(const char* name, void (*fn)()) {
    TestCase t = {name, fn};
    registry().push_back(t);
  }
};

#define TEST(name)                         \
  static void name();                      \
  static Registrar registrar_##name(#name, name); \
  static void name()

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      g_failures++;                                                          \
      std::cout << "  FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "\n"; \
    }                                                                        \
  } while (0)

std::string slurp(const std::string& path);
time_t localEpoch(int year, int month, int day, int hour, int minute, int second);
bool isValidJson(const std::string& text);
