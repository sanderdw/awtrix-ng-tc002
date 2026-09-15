#include "core/render/DisplayScale.h"
#include "core/render/ProvisioningScreen.h"

#include "core/render/HsvText.h"

namespace awtrix {
namespace render {

void drawProvisioningScreen(Canvas& c, const GfxFont& font, int64_t nowMs) {
#ifdef AWTRIX_TC002
  if (c.height() == 16) {
    Canvas legacy(32, 8);
    drawProvisioningScreen(legacy, font, nowMs);
    fitCanvas(legacy, c);
    return;
  }
#endif
  c.clear(0x000000u);
  drawHsvText(c, font, 2, 6, "AP MODE", nowMs);
}

}
}
