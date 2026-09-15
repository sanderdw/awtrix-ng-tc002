#pragma once

#include <algorithm>
#include "core/render/Canvas.h"
#include "core/render/RenderPipeline.h"
#include "media/GifPlayer.h"
#include "media/IconRenderer.h"
#include "media/MicroGif.h"

namespace awtrix {

class DevicePageIcon : public IPageIcon {
 public:
  bool begin(const std::string& iconId) override {
    clear();
    buf_.clear();
    if (gif_.open(iconId, false, 1) == GifPlayer::OpenResult::kGood) {
      sourceWidth_ = gif_.width();
      sourceHeight_ = gif_.height();
    } else if (!icon::draw(buf_, iconId, 0, 0, &sourceWidth_, &sourceHeight_)) {
      return false;
    }
    width_ = sourceWidth_;
    height_ = sourceHeight_;
#ifdef AWTRIX_TC002
    // Classic tiles grow to 16x16; classic full-screen art fits the 52x16 panel.
    // Native assets keep every source pixel, including full-screen 52x16 GIFs.
    if (sourceHeight_ <= 8) {
      width_ = std::min(52, sourceWidth_ * 2);
      height_ = sourceHeight_ * 2;
    }
#endif
    return width_ > 0 && height_ > 0;
  }
  void clear() override {
    gif_.close();
    width_ = height_ = sourceWidth_ = sourceHeight_ = 0;
  }
  void advance(int64_t nowMs) override {
    if (gif_.active()) gif_.render(buf_, nowMs);
  }
  void blit(Canvas& dst, int xOffset) const override {
    const int top = (dst.height() - height_) / 2;
    for (int y = 0; y < height_; ++y)
      for (int x = 0; x < width_; ++x)
        dst.setPixel(x + xOffset, y + top,
          buf_.getPixel(x * sourceWidth_ / width_, y * sourceHeight_ / height_));
  }
  int width() const override { return width_; }

 private:
  Canvas buf_{media::MicroGif::kMaxW, media::MicroGif::kMaxH};
  GifPlayer gif_;
  int width_ = 0, height_ = 0, sourceWidth_ = 0, sourceHeight_ = 0;
};

}
