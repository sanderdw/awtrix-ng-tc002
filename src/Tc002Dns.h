#pragma once
#include <string>
#include <vector>
namespace awtrix { struct DeviceConfig; }
namespace tc002 {
std::string resolverConfig(const std::vector<std::string>& servers);

// Gives AWTRIX's libc resolver the vendor DHCP DNS settings. The stock root
// filesystem is read-only; the bind mount exists only in our mount namespace.
class Dns {
 public:
  ~Dns();
  void begin(const awtrix::DeviceConfig& config);
  void refresh(const awtrix::DeviceConfig& config);
  void stop();
 private:
  std::string path_, current_;
  bool mounted_ = false;
};
}
