#pragma once

#include <algorithm>

#include "core/render/Canvas.h"

namespace awtrix {

// Nearest-neighbour presentation keeps LED art sharp. Explicit drawing coordinates remain physical
// pixels; only legacy 32 by 8 layouts are presented through a fitted canvas.
inline void fitCanvas(const Canvas& source, Canvas& target) {
  for (int y = 0; y < target.height(); ++y)
    for (int x = 0; x < target.width(); ++x)
      target.setPixel(x, y, source.getPixel(x * source.width() / target.width(),
                                            y * source.height() / target.height()));
}

// Give date/time formats room to expand before fitting them. Rendering directly into 32 columns
// would crop weekday names and four-digit years.
template <typename Draw>
void renderLegacyFitted(Canvas& dst, Draw draw) {
  if (dst.height() != 16) {
    draw(dst);
    return;
  }
  Canvas legacy(128, 8);
  draw(legacy);
  int halfWidth = 16;
  for (int y = 0; y < legacy.height(); ++y)
    for (int x = 0; x < legacy.width(); ++x)
      if (legacy.getPixel(x, y)) halfWidth = std::max(halfWidth, x < 64 ? 64 - x : x - 63);
  for (int y = 0; y < dst.height(); ++y)
    for (int x = 0; x < dst.width(); ++x)
      dst.setPixel(x, y, legacy.getPixel(64 - halfWidth + x * halfWidth * 2 / dst.width(), y / 2));
}

}
