#include "util.h"

#include <stdio.h>
#include <string.h>

#include "config.h"

namespace util {

bool parseRelayIndex(const String& text, uint8_t& index) {
  size_t len = text.length();
  if (len == 0 || len > 2) return false;
  unsigned value = 0;
  for (size_t i = 0; i < len; i++) {
    char c = text[i];
    if (c < '0' || c > '9') return false;
    value = value * 10 + (unsigned)(c - '0');
  }
  if (value >= cfg::kRelayCount) return false;
  index = (uint8_t)value;
  return true;
}

bool parseBool(const String& text, bool& value) {
  if (text == "1" || text.equalsIgnoreCase("true") || text.equalsIgnoreCase("on")) {
    value = true;
    return true;
  }
  if (text == "0" || text.equalsIgnoreCase("false") || text.equalsIgnoreCase("off")) {
    value = false;
    return true;
  }
  return false;
}

bool parseHHMM(const String& text, uint8_t& hour, uint8_t& minute) {
  if (text.length() != 5 || text[2] != ':') return false;
  const int pos[4] = {0, 1, 3, 4};
  int digit[4];
  for (int i = 0; i < 4; i++) {
    char c = text[pos[i]];
    if (c < '0' || c > '9') return false;
    digit[i] = c - '0';
  }
  int h = digit[0] * 10 + digit[1];
  int m = digit[2] * 10 + digit[3];
  if (h > 23 || m > 59) return false;
  hour = (uint8_t)h;
  minute = (uint8_t)m;
  return true;
}

bool isValidUtf8(const String& text) {
  const uint8_t* p = (const uint8_t*)text.c_str();
  size_t n = text.length();
  size_t i = 0;
  while (i < n) {
    uint8_t c = p[i];
    if (c < 0x80) {
      i++;
      continue;
    }
    size_t need;
    uint32_t cp;
    if ((c & 0xE0) == 0xC0) {
      if (c < 0xC2) return false;
      need = 1;
      cp = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
      need = 2;
      cp = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
      if (c > 0xF4) return false;
      need = 3;
      cp = c & 0x07;
    } else {
      return false;
    }
    if (i + need >= n) return false;
    for (size_t k = 1; k <= need; k++) {
      if ((p[i + k] & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (p[i + k] & 0x3F);
    }
    if (need == 2 && cp < 0x800) return false;
    if (need == 3 && (cp < 0x10000 || cp > 0x10FFFF)) return false;
    if (cp >= 0xD800 && cp <= 0xDFFF) return false;
    i += need + 1;
  }
  return true;
}

bool validName(const String& text) {
  size_t len = text.length();
  if (len == 0 || len > cfg::kNameMaxLen) return false;
  for (size_t i = 0; i < len; i++) {
    uint8_t c = (uint8_t)text[i];
    if (c < 0x20 || c == 0x7F) return false;
  }
  return isValidUtf8(text);
}

String jsonEscape(const String& text) {
  String out;
  size_t len = text.length();
  for (size_t i = 0; i < len; i++) {
    char c = text[i];
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if ((uint8_t)c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)(uint8_t)c);
          out += buf;
        } else {
          out += String(c);
        }
    }
  }
  return out;
}

bool constantTimeEquals(const String& a, const String& b) {
  size_t la = a.length();
  size_t lb = b.length();
  unsigned diff = (unsigned)(la ^ lb);
  for (size_t i = 0; i < lb; i++) {
    uint8_t ca = i < la ? (uint8_t)a[i] : 0;
    diff |= (unsigned)(ca ^ (uint8_t)b[i]);
  }
  return diff == 0;
}

bool isPlaceholder(const char* text) {
  return strstr(text, "change-me") != nullptr || strstr(text, "your-") != nullptr;
}

bool parseSemver(const String& text, int& major, int& minor, int& patch) {
  int consumed = 0;
  if (sscanf(text.c_str(), "%d.%d.%d%n", &major, &minor, &patch, &consumed) != 3) return false;
  if (consumed != (int)text.length()) return false;
  return major >= 0 && minor >= 0 && patch >= 0;
}

bool isNewerVersion(const String& remote, const String& local) {
  int r1, r2, r3, l1, l2, l3;
  if (!parseSemver(remote, r1, r2, r3) || !parseSemver(local, l1, l2, l3)) return false;
  if (r1 != l1) return r1 > l1;
  if (r2 != l2) return r2 > l2;
  return r3 > l3;
}

namespace {
int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
}  // namespace

bool hexToBytes(const String& hex, uint8_t* out, size_t outLen) {
  if (hex.length() != outLen * 2) return false;
  for (size_t i = 0; i < outLen; i++) {
    int hi = hexValue(hex[i * 2]);
    int lo = hexValue(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = (uint8_t)((hi << 4) | lo);
  }
  return true;
}

String manifestValue(const String& manifest, const char* key) {
  String needle = String(key) + "=";
  int pos = 0;
  int total = (int)manifest.length();
  while (pos < total) {
    int end = manifest.indexOf('\n', pos);
    if (end < 0) end = total;
    String line = manifest.substring(pos, end);
    line.trim();
    if (line.startsWith(needle)) return line.substring(needle.length());
    pos = end + 1;
  }
  return "";
}

}  // namespace util
