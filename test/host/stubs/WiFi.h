#pragma once
#include "Arduino.h"

enum wl_status_t { WL_CONNECTED = 3, WL_DISCONNECTED = 6 };
enum WiFiMode_t { WIFI_STA = 1, WIFI_AP_STA = 3 };
enum wifi_auth_mode_t { WIFI_AUTH_OPEN = 0, WIFI_AUTH_WEP = 1, WIFI_AUTH_WPA2_PSK = 3 };
#define WIFI_SCAN_RUNNING (-1)
#define WIFI_SCAN_FAILED (-2)

struct ScanEntry {
  std::string ssid;
  int rssi;
  int enc;
  int channel;
};

struct WiFiClass {
  bool mode(WiFiMode_t);
  void begin(const char*, const char*);
  bool config(IPAddress, IPAddress, IPAddress, IPAddress = IPAddress(), IPAddress = IPAddress());
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
  int16_t scanNetworks(bool async = false, bool showHidden = false, bool passive = false, uint32_t maxMsPerChan = 300,
                       uint8_t channel = 0, const char* ssid = nullptr, const uint8_t* bssid = nullptr);
  int16_t scanComplete();
  void scanDelete();
  String SSID(uint8_t i);
  int RSSI(uint8_t i);
  wifi_auth_mode_t encryptionType(uint8_t i);
  int channel(uint8_t i);
};
extern WiFiClass WiFi;
extern wl_status_t g_wifiStatus;
extern IPAddress g_wifiIp;
extern std::map<std::string, std::string> g_wifiNetworks;
extern std::vector<ScanEntry> g_scanEntries;
extern int g_scanState;
extern int g_scanStarts;
extern int g_softApStarts;
extern int g_softApStops;
extern std::string g_lastBeginSsid;

class WiFiClient {
 public:
  int available();
  size_t readBytes(uint8_t*, size_t);
  bool connected();
};
