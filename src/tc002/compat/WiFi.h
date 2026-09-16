#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>

#include <arpa/inet.h>

#include "IPAddress.h"
#include "Tc002Hardware.h"

// TC002 stand-in for the Arduino WiFi object. Upstream transport code (Art-Net, MQTT) asks it for
// the address and MAC; the answers come from the port's network layer instead of a fixed loopback.
class WiFiClass {
 public:
  bool isConnected() const { return true; }
  IPAddress localIP() const { return IPAddress(inet_addr(tc002::ipAddress().c_str())); }
  int32_t RSSI() const { return tc002::wifiRssi(); }
  const char* getHostname() const { return hostname_; }
  void setHostname(const char* h) { hostname_ = h ? h : ""; }
  void macAddress(uint8_t* mac) const {
    const std::string uid = tc002::uid();
    for (int i = 0; i < 6; ++i)
      mac[i] = static_cast<uint8_t>(std::strtoul(uid.substr(i * 2, 2).c_str(), nullptr, 16));
  }

 private:
  const char* hostname_ = "";
};

inline WiFiClass WiFi;
