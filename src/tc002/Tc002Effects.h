#pragma once

#include "LegacyFit.h"
#include "core/effects/effects/MoreEffects.h"

namespace awtrix {

// The eyes are drawn for an 8-row panel; present them at 32 by 8 and fit to the panel.
class Tc002LookingEyesEffect : public LookingEyesEffect {
 public:
  void render(Canvas& c, int64_t f) override {
    if (c.height() != 16) {
      LookingEyesEffect::render(c, f);
      return;
    }
    Canvas legacy(c.width() / 2, 8);
    LookingEyesEffect::render(legacy, f);
    fitCanvas(legacy, c);
  }
};

}
