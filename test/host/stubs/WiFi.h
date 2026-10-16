#pragma once
#include "Arduino.h"

enum wl_status_t { WL_CONNECTED = 3, WL_DISCONNECTED = 6 };
enum WiFiMode_t { WIFI_STA = 1, WIFI_AP_STA = 3 };

struct WiFiClass {
  bool mode(WiFiMode_t);
  void begin(const char*, const char*);
  bool config(IPAddress, IPAddress, IPAddress, IPAddress);
  bool disconnect(bool = false);
  wl_status_t status();
  IPAddress localIP();
  IPAddress gatewayIP();
  IPAddress subnetMask();
  IPAddress dnsIP();
  IPAddress softAPIP();
  bool softAP(const char*, const char*);
  bool softAPdisconnect(bool = false);
  void persistent(bool);
  int RSSI();
};
extern WiFiClass WiFi;
extern wl_status_t g_wifiStatus;
extern IPAddress g_wifiIp;

class WiFiClient {
 public:
  int available();
  size_t readBytes(uint8_t*, size_t);
  bool connected();
};
