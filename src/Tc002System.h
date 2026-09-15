#pragma once
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
namespace awtrix { struct DeviceConfig; }

namespace tc002 {
// Returns a cached scan and asks the background service to refresh it.
std::string wifiScan();
std::string wifiSsid();
bool wifiConnected();
bool wifiApMode();
void resetWifi();
int dhcpOnce();
class System {
 public:
  ~System();
  void stop();
  void begin(const awtrix::DeviceConfig& config);
 private:
  std::atomic<bool> stop_{false};
  std::thread worker_;
  std::mutex mutex_;
  std::condition_variable wake_;
};
}
