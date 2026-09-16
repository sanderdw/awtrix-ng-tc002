#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace tc002 {
constexpr int Width = 52, Height = 16, Stride = 64;
using Frame = std::array<uint8_t, Stride * Height * 3>;
// RGB, row-major; the twelve unused columns must remain black.
Frame packFrame(const uint32_t* pixels, bool mirror, bool rotate);
std::vector<uint8_t> mcuPacket(uint8_t command, const std::vector<uint8_t>& payload = {});
bool parseMcuPacket(std::vector<uint8_t>& buffer, uint8_t& command, std::vector<uint8_t>& payload);
std::string uid();
std::string ipAddress();
long availableMemory();
int wifiRssi();
bool batteryAvailable();
int batteryPercent();
const char* caCertPath();
bool hardwareEnabled();
void setHardwareEnabled(bool enabled);

class Hardware {
 public:
  ~Hardware();
  void begin();
  void show(const uint32_t* pixels, bool mirror, bool rotate);
  void poll(bool& left, bool& select, bool& right, int& rotation);
  int batteryMillivolts() const { return batteryMv_; }
  int batteryPercent() const { return batteryPercent_; }
  bool batteryAvailable() const { return batteryMv_ > 0; }
  void tickMcu();
 private:
  int spi_ = -1, gpio_ = -1, uart_ = -1;
  int inputs_[2] = {-1, -1};
  bool keys_[4] = {};
  int lastRotation_ = 0;
  int64_t lastFrameUs_ = 0;
  int64_t lastBatteryMs_ = 0;
  int batteryMv_ = -1;
  int batteryPercent_ = -1;
  std::vector<uint8_t> rx_;
  std::string mcuVersion_;
  void query(uint8_t cmd);
};
}
