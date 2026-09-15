#pragma once

#include "core/render/Canvas.h"

namespace awtrix {

// Nearest-neighbour presentation keeps LED art sharp. Explicit drawing coordinates
// remain physical pixels; only legacy built-in layouts use a fitted canvas.
inline void fitCanvas(const Canvas& source, Canvas& target) {
  for (int y = 0; y < target.height(); ++y)
    for (int x = 0; x < target.width(); ++x)
      target.setPixel(x, y, source.getPixel(x * source.width() / target.width(),
                                          y * source.height() / target.height()));
}

inline int displayTextScale(int height) {
#ifdef AWTRIX_TC002
  return height >= 16 ? 2 : 1;
#else
  return 1;
#endif
}

}
