#pragma once
#include "Arduino.h"

enum HTTPMethod { HTTP_ANY, HTTP_GET, HTTP_POST };

struct HttpResponse {
  int code;
  std::string type;
  std::string body;
  std::map<std::string, std::string> headers;
};

class WebServer {
 public:
  typedef std::function<void()> THandlerFunction;
  struct Route {
    std::string uri;
    HTTPMethod method;
    THandlerFunction fn;
  };
  WebServer(int) { instance = this; }
  void on(const char* uri, THandlerFunction fn);
  void on(const char* uri, HTTPMethod method, THandlerFunction fn);
  void onNotFound(THandlerFunction fn);
  void collectHeaders(const char* keys[], const size_t count);
  void begin();
  void handleClient();
  bool hasArg(const String& name);
  String arg(const String& name);
  String header(const String& name);
  void sendHeader(const String& name, const String& value, bool first = false);
  void send(int code, const char* type, const String& body);
  void send_P(int code, const char* type, const char* body);

  static WebServer* instance;
  std::vector<Route> routes;
  THandlerFunction notFound;
  std::map<std::string, std::string> args;
  std::map<std::string, std::string> headers;
  HttpResponse response;
  HttpResponse perform(HTTPMethod method, const std::string& uri);
};
