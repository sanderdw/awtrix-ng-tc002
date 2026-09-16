// TC002 replacement for upstream's transport/DeviceStateJson.cpp: the same MQTT device document,
// with the facts taken from the clock's Linux system instead of the ESP32 SDK.
#include "transport/DeviceStateJson.h"

#include <WiFi.h>

#include "Tc002Hardware.h"
#include "core/CoreEngine.h"
#include "hal/IBoard.h"
#include "system/MonotonicClock.h"
#include "transport/http/UpdateImage.h"

namespace awtrix {

std::string buildDeviceStateJson(CoreEngine& engine, IBoard& board, const std::string& uid,
                                 bool scriptingRunning) {
  ISensorBus& sensors = board.sensors();
  DeviceFacts facts;
  facts.boardType = "tc002";
  facts.soc = "ssd202d";
  facts.updateImage = kUpdateImageName;  // empty: this port has no ESP32-style image feed
  facts.ipAddress = tc002::ipAddress();
  const char* hn = WiFi.getHostname();
  facts.hostname = hn ? hn : "";
  facts.wifiRssi = tc002::wifiRssi();
  facts.uptimeSeconds = static_cast<long>(monotonicMs() / 1000);
  facts.freeHeapBytes = tc002::availableMemory();
  facts.minFreeHeapBytes = 0;
  facts.largestFreeBlockBytes = 0;
  facts.psramTotalBytes = 0;
  facts.psramFreeBytes = 0;
  // Starting the binary is always a clean power-on; there is no reset cause to read on Linux.
  facts.resetReason = "poweron";
  facts.hasBattery = board.hasBattery();
  facts.hasLightSensor = board.hasLightSensor();
  facts.hasTemperature = sensors.hasSensor();
  facts.hasHumidity = sensors.hasHumidity();
  facts.hasPressure = sensors.hasPressure();
  facts.scriptingRunning = scriptingRunning;
  return buildDeviceJson(engine, uid, facts);
}

}
