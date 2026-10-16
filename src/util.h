#pragma once

#include <Arduino.h>

namespace util {
bool parseRelayIndex(const String& text, uint8_t& index);
bool parseBool(const String& text, bool& value);
bool parseHHMM(const String& text, uint8_t& hour, uint8_t& minute);
bool validName(const String& text);
bool isValidUtf8(const String& text);
String jsonEscape(const String& text);
bool constantTimeEquals(const String& a, const String& b);
bool isPlaceholder(const char* text);
bool parseSemver(const String& text, int& major, int& minor, int& patch);
bool isNewerVersion(const String& remote, const String& local);
bool hexToBytes(const String& hex, uint8_t* out, size_t outLen);
String manifestValue(const String& manifest, const char* key);
}  // namespace util
