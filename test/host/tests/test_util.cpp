#include <config.h>

#include "../../../src/util.h"
#include "testlib.h"

TEST(util_relay_index) {
  uint8_t i = 9;
  CHECK(util::parseRelayIndex("0", i) && i == 0);
  CHECK(util::parseRelayIndex("3", i) && i == 3);
  CHECK(util::parseRelayIndex("03", i) && i == 3);
  CHECK(!util::parseRelayIndex("4", i));
  CHECK(!util::parseRelayIndex("abc", i));
  CHECK(!util::parseRelayIndex("", i));
  CHECK(!util::parseRelayIndex("-1", i));
  CHECK(!util::parseRelayIndex("1x", i));
  CHECK(!util::parseRelayIndex("100", i));
}

TEST(util_bool) {
  bool v = false;
  CHECK(util::parseBool("1", v) && v);
  CHECK(util::parseBool("true", v) && v);
  CHECK(util::parseBool("ON", v) && v);
  CHECK(util::parseBool("0", v) && !v);
  CHECK(util::parseBool("False", v) && !v);
  CHECK(util::parseBool("off", v) && !v);
  CHECK(!util::parseBool("", v));
  CHECK(!util::parseBool("2", v));
  CHECK(!util::parseBool("yes", v));
}

TEST(util_hhmm) {
  uint8_t h = 0, m = 0;
  CHECK(util::parseHHMM("07:05", h, m) && h == 7 && m == 5);
  CHECK(util::parseHHMM("23:59", h, m) && h == 23 && m == 59);
  CHECK(util::parseHHMM("00:00", h, m) && h == 0 && m == 0);
  CHECK(!util::parseHHMM("7:05", h, m));
  CHECK(!util::parseHHMM("24:00", h, m));
  CHECK(!util::parseHHMM("12:60", h, m));
  CHECK(!util::parseHHMM("12-30", h, m));
  CHECK(!util::parseHHMM("12:30:00", h, m));
  CHECK(!util::parseHHMM("ab:cd", h, m));
  CHECK(!util::parseHHMM("", h, m));
}

TEST(util_names_and_utf8) {
  CHECK(util::validName("Porch light"));
  CHECK(util::validName("Caf\xC3\xA9"));
  CHECK(util::validName("quote\" and \\ backslash"));
  CHECK(!util::validName(""));
  CHECK(!util::validName(String(std::string(25, 'a').c_str())));
  CHECK(util::validName(String(std::string(24, 'a').c_str())));
  CHECK(!util::validName("tab\there"));
  CHECK(!util::validName("new\nline"));
  CHECK(!util::validName("del\x7f"));
  CHECK(!util::validName("bad\xFF"));
  CHECK(!util::validName("trunc\xC3"));
  CHECK(!util::validName("overlong\xC0\x80"));
  CHECK(!util::validName("surrogate\xED\xA0\x80"));
  CHECK(util::validName("\xF0\x9F\x92\xA1"));
}

TEST(util_json_escape) {
  CHECK(util::jsonEscape("plain") == "plain");
  CHECK(util::jsonEscape("a\"b") == "a\\\"b");
  CHECK(util::jsonEscape("a\\b") == "a\\\\b");
  CHECK(util::jsonEscape("a\nb") == "a\\nb");
  String ctrl;
  ctrl += String((char)1);
  CHECK(util::jsonEscape(ctrl) == "\\u0001");
  CHECK(isValidJson("\"" + util::jsonEscape("x\"\\\n\t\r/") .s + "\""));
}

TEST(util_constant_time_equals) {
  CHECK(util::constantTimeEquals("secret-token-123456", "secret-token-123456"));
  CHECK(!util::constantTimeEquals("secret-token-123456", "secret-token-123457"));
  CHECK(!util::constantTimeEquals("secret", "secret-token-123456"));
  CHECK(!util::constantTimeEquals("secret-token-123456", "secret"));
  CHECK(!util::constantTimeEquals("", "x"));
  CHECK(util::constantTimeEquals("", ""));
}

TEST(util_placeholder) {
  CHECK(util::isPlaceholder("change-me-please"));
  CHECK(util::isPlaceholder("your-github-user"));
  CHECK(!util::isPlaceholder("realvalue123"));
}

TEST(util_semver) {
  int a, b, c;
  CHECK(util::parseSemver("1.2.3", a, b, c) && a == 1 && b == 2 && c == 3);
  CHECK(!util::parseSemver("v1.2.3", a, b, c));
  CHECK(!util::parseSemver("1.2", a, b, c));
  CHECK(!util::parseSemver("1.2.3.4", a, b, c));
  CHECK(!util::parseSemver("1.2.3 ", a, b, c));
  CHECK(!util::parseSemver("-1.2.3", a, b, c));
  CHECK(!util::parseSemver("", a, b, c));
  CHECK(util::isNewerVersion("1.0.1", "1.0.0"));
  CHECK(!util::isNewerVersion("1.0.0", "1.0.0"));
  CHECK(!util::isNewerVersion("0.9.9", "1.0.0"));
  CHECK(util::isNewerVersion("1.10.0", "1.9.0"));
  CHECK(!util::isNewerVersion("garbage", "1.0.0"));
}

TEST(util_manifest_and_hex) {
  String manifest = "version=1.2.3\r\nsize=123456\nsha256=00ff\n";
  CHECK(util::manifestValue(manifest, "version") == "1.2.3");
  CHECK(util::manifestValue(manifest, "size") == "123456");
  CHECK(util::manifestValue(manifest, "missing") == "");
  uint8_t out[2];
  CHECK(util::hexToBytes("00fF", out, 2) && out[0] == 0x00 && out[1] == 0xFF);
  CHECK(!util::hexToBytes("zz00", out, 2));
  CHECK(!util::hexToBytes("00", out, 2));
}

TEST(util_wifi_and_guest_validators) {
  CHECK(util::validSsid("Home Wi-Fi"));
  CHECK(util::validSsid(String(std::string(32, 'a').c_str())));
  CHECK(!util::validSsid(String(std::string(33, 'a').c_str())));
  CHECK(!util::validSsid(""));
  CHECK(!util::validSsid("bad\x01"));
  CHECK(!util::validSsid("bad\xFF"));
  CHECK(util::validWifiPassword(""));
  CHECK(util::validWifiPassword("12345678"));
  CHECK(util::validWifiPassword(String(std::string(63, 'a').c_str())));
  CHECK(!util::validWifiPassword("1234567"));
  CHECK(!util::validWifiPassword(String(std::string(64, 'a').c_str())));
  CHECK(util::validWifiPassword(" spaced pass "));
  CHECK(util::validGuestPassword("guest-pass-1234"));
  CHECK(!util::validGuestPassword("short"));
  CHECK(!util::validGuestPassword(" leading-space1"));
  CHECK(!util::validGuestPassword("trailing-space1 "));
  CHECK(util::validGuestPassword(String(std::string(64, 'a').c_str())));
  CHECK(!util::validGuestPassword(String(std::string(65, 'a').c_str())));
  CHECK(util::hasControlChars("a\tb"));
  CHECK(!util::hasControlChars("plain"));
}
