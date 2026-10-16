#include <fstream>
#include <sstream>

#include "testlib.h"

int g_failures = 0;
std::string g_fixtureDir;

std::vector<TestCase>& registry() {
  static std::vector<TestCase> tests;
  return tests;
}

std::string slurp(const std::string& path) {
  std::ifstream f(path.c_str(), std::ios::binary);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}

time_t localEpoch(int year, int month, int day, int hour, int minute, int second) {
  struct tm t;
  memset(&t, 0, sizeof(t));
  t.tm_year = year - 1900;
  t.tm_mon = month - 1;
  t.tm_mday = day;
  t.tm_hour = hour;
  t.tm_min = minute;
  t.tm_sec = second;
  return timegm(&t) - 19800;
}

bool isValidJson(const std::string& text) {
  std::ofstream("/tmp/relay_test_json.txt", std::ios::binary) << text;
  return system("python3 -c \"import json;json.load(open('/tmp/relay_test_json.txt','rb'))\" >/dev/null 2>&1") == 0;
}

int main(int argc, char** argv) {
  if (argc >= 2 && std::string(argv[1]) == "--list") {
    for (size_t i = 0; i < registry().size(); i++) std::cout << registry()[i].name << "\n";
    return 0;
  }
  if (argc < 3) {
    std::cerr << "usage: runner --list | runner <test> <fixture-dir>\n";
    return 2;
  }
  g_fixtureDir = argv[2];
  for (size_t i = 0; i < registry().size(); i++) {
    if (std::string(registry()[i].name) == argv[1]) {
      registry()[i].fn();
      std::cout << (g_failures ? "FAIL  " : "ok    ") << argv[1] << "\n";
      return g_failures ? 1 : 0;
    }
  }
  std::cerr << "unknown test " << argv[1] << "\n";
  return 2;
}
