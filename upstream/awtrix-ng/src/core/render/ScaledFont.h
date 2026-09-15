#pragma once

#include <algorithm>
#include <vector>
#include "core/render/Font.h"

namespace awtrix {

// Own the expanded bitmaps so every text measurement, scroll bound and renderer
// sees the same metrics, including accented glyphs in sparse Unicode ranges.
class ScaledFont {
 public:
  void reset(const GfxFont& source, int scale) {
    font_ = source;
    bitmap_.clear();
    glyphs_.clear();
    int count = source.last - source.first + 1;
    for (int r = 0; r < source.rangeCount; ++r) {
      const auto& range = source.ranges[r];
      for (int c = 0; c <= range.last - range.first; ++c)
        count = std::max(count, static_cast<int>(range.index[c]));
    }
    for (int i = 0; i < count; ++i) {
      const auto& g = source.glyphs[i];
      FontGlyph out = g;
      out.bitmapOffset = bitmap_.size();
      out.width *= scale; out.height *= scale; out.xAdvance *= scale;
      out.xOffset *= scale; out.yOffset *= scale;
      const auto start = bitmap_.size();
      bitmap_.resize(start + (out.width * out.height + 7) / 8, 0);
      for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x) {
          const int bit = (y / scale) * g.width + x / scale;
          if (source.bitmap[g.bitmapOffset + bit / 8] & (0x80 >> (bit % 8))) {
            const int dest = y * out.width + x;
            bitmap_[start + dest / 8] |= 0x80 >> (dest % 8);
          }
        }
      glyphs_.push_back(out);
    }
    font_.yAdvance *= scale;
    font_.bitmap = bitmap_.data();
    font_.glyphs = glyphs_.data();
  }
  const GfxFont& font() const { return font_; }
 private:
  GfxFont font_{};
  std::vector<uint8_t> bitmap_;
  std::vector<FontGlyph> glyphs_;
};

}
