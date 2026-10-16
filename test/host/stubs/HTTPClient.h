#pragma once
#include "WiFi.h"

#define HTTP_CODE_OK 200
enum followRedirects_t { HTTPC_DISABLE_FOLLOW_REDIRECTS, HTTPC_STRICT_FOLLOW_REDIRECTS, HTTPC_FORCE_FOLLOW_REDIRECTS };

extern std::map<std::string, std::string> g_files;

class HTTPClient {
 public:
  bool begin(WiFiClient&, String);
  void setFollowRedirects(followRedirects_t);
  void setTimeout(uint16_t);
  int GET();
  int getSize();
  WiFiClient* getStreamPtr();
  void end();
  bool connected();
};
