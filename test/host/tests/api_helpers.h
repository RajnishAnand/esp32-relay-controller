#pragma once

#include <config.h>

#include <WebServer.h>

#include "../../../src/api.h"
#include "../../../src/network.h"
#include "../../../src/relays.h"
#include "../../../src/scheduler.h"
#include "testlib.h"

static const char* kToken = "0123456789abcdef0123456789abcdef";
typedef std::map<std::string, std::string> Args;

static inline void boot() {
  g_nvs["api_token"] = kToken;
  relays::begin();
  scheduler::begin();
  network::begin();
  api::begin();
}

static inline HttpResponse call(HTTPMethod method, const char* uri, const Args& args = Args(), const char* token = kToken) {
  WebServer* s = WebServer::instance;
  s->args = args;
  s->headers.clear();
  if (token) s->headers["X-API-Key"] = token;
  return s->perform(method, uri);
}

static inline Args args(const char* k1, const char* v1, const char* k2 = nullptr, const char* v2 = nullptr,
                 const char* k3 = nullptr, const char* v3 = nullptr, const char* k4 = nullptr,
                 const char* v4 = nullptr) {
  Args a;
  a[k1] = v1;
  if (k2) a[k2] = v2;
  if (k3) a[k3] = v3;
  if (k4) a[k4] = v4;
  return a;
}

static inline bool has(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

