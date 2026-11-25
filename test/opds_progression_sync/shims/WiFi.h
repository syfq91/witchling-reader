#pragma once
#include <string>

inline constexpr int WL_CONNECTED = 3;
inline constexpr int WL_DISCONNECTED = 6;

class WiFiMock {
 public:
  int currentStatus = WL_CONNECTED;
  std::string mac = "AA:BB:CC:DD:EE:FF";

  int status() const { return currentStatus; }
  std::string macAddress() const { return mac; }
};

inline WiFiMock WiFi;
