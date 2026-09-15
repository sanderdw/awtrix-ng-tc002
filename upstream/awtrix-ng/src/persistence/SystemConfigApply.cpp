#include "persistence/SystemConfigApply.h"

#include "core/ConfigRules.h"

namespace awtrix {
namespace sysconfig {

bool apply(DeviceConfig& cfg, api::JsonReader obj, int& applied, ApplyError& err, Origin origin) {
  applied = 0;
  cfgrules::ConfigError cerr;
  // A restore is allowed to write empty strings to clear a field; an interactive edit is not,
  // because a blank box in the UI means "leave it alone".
  const bool allowEmptyClears = origin == Origin::Restore;
  if (!cfgrules::validateSystemRead(obj, cerr, allowEmptyClears)) {
    err = {422, "validationFailed", cerr.message, cerr.field};
    return false;
  }
  // Everything is merged into a copy and only assigned back once all the cross-field rules pass,
  // so a rejected request cannot leave the live config half applied.
  DeviceConfig merged = cfg;
  const std::string ssid = merged.wifiSsid, pass = merged.wifiPass;
  applied = merged.applyRead(obj);
#ifdef AWTRIX_TC002
  if(merged.panelWidth!=52 || merged.panels!=1) {
    err={422,"validationFailed","TC002 has a fixed 52 by 16 pixel matrix","panelWidth"};
    return false;
  }
#define FIXED(key) if(merged.key!=cfg.key) { \
  err={422,"validationFailed","TC002 hardware wiring is fixed",#key}; return false; }
  FIXED(panelStart) FIXED(panelWiring) FIXED(panelSerpentine)
  FIXED(panelChainReverse) FIXED(panelChainSerpentine) FIXED(dfplayer)
  FIXED(pinMatrix) FIXED(pinBtnLeft) FIXED(pinBtnSelect) FIXED(pinBtnRight)
  FIXED(pinBattery) FIXED(pinLdr) FIXED(pinBuzzer) FIXED(pinI2cSda) FIXED(pinI2cScl)
  FIXED(pinDfRx) FIXED(pinDfTx) FIXED(pinI2sBclk) FIXED(pinI2sLrclk) FIXED(pinI2sDout)
#undef FIXED
#endif
  // A restored backup keeps the credentials this device is connected with — the backup may come
  // from another network, and taking its Wi-Fi settings would strand the device.
  if (origin == Origin::Restore) {
    merged.wifiSsid = ssid;
    merged.wifiPass = pass;
  }
  const cfgrules::IpSplit split = cfgrules::systemIpSplit(obj);
  if (split.present) {
    merged.ip = split.ip;
    merged.subnet = split.subnet;
    ++applied;
  }
  if (!cfgrules::validateMatrixGeometry(merged.panelWidth, merged.panels, cerr) ||
      !cfgrules::validateBrightnessWindow(merged.minBrightness, merged.maxBrightness, cerr) ||
      !cfgrules::validateStaticNet(merged.netStatic, merged.ip, merged.subnet, cerr) ||
      !cfgrules::validateMqttGate(merged.mqttEnabled, merged.mqttHost, cerr) ||
      !cfgrules::validateAuthGate(merged.authEnabled, merged.authUser, merged.authPass, cerr) ||
      !cfgrules::validateAudioPins(merged.pinI2sBclk, merged.pinI2sLrclk, merged.pinI2sDout,
                                   cerr)) {
    err = {422, "validationFailed", cerr.message, cerr.field};
    return false;
  }
  std::string pinErr;
  if (!merged.validatePins(pinErr)) {
    err = {400, "invalidPinConfig", pinErr, ""};
    return false;
  }
  cfg = merged;
  return true;
}

}
}
