#pragma once

#include <string>
#include <vector>

#include "core/Transitions.h"
#include "core/sound/AudioRouter.h"

namespace awtrix {

// Same document as api::capabilitiesJson, with the clock's fixed 52 by 16 matrix and no
// configurable GPIO in place of the ESP32 pin profile.
inline std::string tc002CapabilitiesJson(const std::vector<std::string>& effects,
                                         const std::vector<std::string>& paletteEffects,
                                         const std::vector<std::string>& overlays,
                                         const sound::Caps& audio) {
  auto list = [](const std::vector<std::string>& names) {
    std::string out = "[";
    bool first = true;
    for (const auto& n : names) {
      if (!first) out += ',';
      out += '"' + n + '"';
      first = false;
    }
    out += ']';
    return out;
  };
  auto flag = [](bool v) { return std::string(v ? "true" : "false"); };
  return "{\"effects\":" + list(effects) + ",\"paletteEffects\":" + list(paletteEffects) +
         ",\"transitions\":" + transitionsJson() + ",\"overlays\":" + list(overlays) +
         ",\"palettes\":[\"Cloud\",\"Lava\",\"Ocean\",\"Forest\",\"Stripe\","
         "\"Party\",\"Heat\",\"Rainbow\"]"
         ",\"audio\":{\"buzzer\":" +
         flag(audio.buzzer) + ",\"track\":" + flag(audio.track) + ",\"mp3\":" + flag(audio.mp3) +
         ",\"radio\":" + flag(audio.radio) + "}" +
         ",\"matrix\":{\"width\":52,\"height\":16,\"fixed\":true}"
         ",\"gpio\":{\"soc\":\"ssd202d\",\"fixed\":true,\"input\":[],\"output\":[],\"adc\":[],\"rtc\":[]}}";
}

}
