#include "core/apps/builtin/Tc002Layout.h"

#include <algorithm>
#include <string>
#include <vector>

#include "core/apps/IApp.h"
#include "core/apps/ClockText.h"
#include "core/apps/SensorFormat.h"
#include "core/apps/builtin/WeekdayBar.h"
#include "core/render/TextEncoding.h"
#include "core/render/TextRenderer.h"

namespace awtrix::tc002layout {
namespace {

bool supported(const Canvas& c) { return c.width() == 52 && c.height() == 16; }

// Draw directly in physical pixels. Every source font pixel is exactly 2x2;
// text layout never resamples a complete framebuffer by a fractional factor.
void glyph2(Canvas& c, const GfxFont& font, char ch, int x, int baseline, uint32_t rgb) {
  const FontGlyph* g = text::glyphFor(font, static_cast<unsigned char>(ch));
  if (!g) return;
  for (int y = 0; y < g->height; ++y)
    for (int col = 0; col < g->width; ++col) {
      const int bit = y * g->width + col;
      if (font.bitmap[g->bitmapOffset + bit / 8] & (0x80 >> (bit % 8)))
        c.fillRect(x + (g->xOffset + col) * 2, baseline + (g->yOffset + y) * 2, 2, 2, rgb);
    }
}

bool punctuation(char ch) { return ch == '.' || ch == '/' || ch == '-' || ch == ':'; }

struct TextLine {
  std::string text;
  std::vector<int> positions;
  int left = 0, right = -1;
  int width() const { return right - left + 1; }
};

TextLine line(const GfxFont& font, const std::string& text, bool compact = false) {
  TextLine out;
  out.text = text;
  int x = 0;
  bool ink = false;
  for (std::size_t i = 0; i < text.size(); ++i) {
    // One physical column on each side of date separators, two between digits.
    if (compact && i && (punctuation(text[i]) || punctuation(text[i - 1]))) --x;
    out.positions.push_back(x);
    const auto m = text::measure(font, std::string(1, text[i]));
    if (m.hasInk()) {
      if (!ink) out.left = x + m.inkLeft * 2;
      out.right = x + m.inkRight * 2 + 1;
      ink = true;
    }
    x += text::charAdvance(font, static_cast<unsigned char>(text[i])) * 2;
  }
  return out;
}

void drawLine(Canvas& c, const GfxFont& font, const TextLine& line, int x, int baseline,
              uint32_t rgb, uint32_t separator) {
  for (std::size_t i = 0; i < line.text.size(); ++i)
    glyph2(c, font, line.text[i], x + line.positions[i], baseline,
           line.text[i] == ':' ? separator : rgb);
}

void centered(Canvas& c, const GfxFont& font, const TextLine& line, int x, int width,
              int baseline, uint32_t rgb, uint32_t separator) {
  drawLine(c, font, line, x + (width - line.width()) / 2 - line.left, baseline, rgb, separator);
}

void weekdays(Canvas& c, const RenderCtx& ctx, int x, int y, int markerWidth) {
  const auto& cfg = ctx.settings->weekdayBar;
  if (!cfg.show) return;
  for (int i = 0; i < 7; ++i)
    c.fillRect(x + i * (markerWidth + 1), y, markerWidth, 2,
               weekdayBarColor(cfg, i, ctx.weekday));
}

uint32_t dim(uint32_t rgb, unsigned level) {
  return (((rgb >> 16 & 255) * level / 255) << 16) |
         (((rgb >> 8 & 255) * level / 255) << 8) | ((rgb & 255) * level / 255);
}
}

bool time(Canvas& c, const RenderCtx& ctx) {
  if (!supported(c)) return false;
  const auto& s = *ctx.settings;
  if (s.timeMode < 0 || s.timeMode > 4) return false;
  const bool calendar = s.timeMode != 0;
  const bool top = s.timeMode == 2 || s.timeMode == 4;
  auto options = timeOptionsFrom(s);
  if (calendar) options.showSeconds = options.showAmPm = false;
  std::string label;
  for (const auto& run : buildTimeRuns(options, ctx.hour, ctx.minute, ctx.second)) label += run.text;
  const int area = calendar ? 34 : 52;
  auto textLine = line(*ctx.font, label);
  if (textLine.width() > area) textLine = line(*ctx.font, label, true);
  if (textLine.width() > area) return false;
  c.clear();
  if (calendar) {
    c.fillRect(0, 0, 16, 16, s.calendarBodyColor);
    if (s.timeMode <= 2) c.fillRect(0, 0, 16, 2, s.calendarHeaderColor);
    else {
      c.fillRect(2, 0, 4, 2, 0);
      c.fillRect(10, 0, 4, 2, 0);
    }
    const auto day = line(*ctx.font, std::to_string(ctx.mday));
    centered(c, *ctx.font, day, 0, 16, 14, s.calendarTextColor, s.calendarTextColor);
  }
  const uint32_t col = s.timeColor.valueOr(s.textColor);
  const uint32_t sep = scaleColor(col, separatorLevel(s.timeSeparatorMode, ctx.second, ctx.nowMs));
  const int baseline = s.weekdayBar.show ? (top ? 14 : 12) : 13;
  centered(c, *ctx.font, textLine, calendar ? 17 : 0, area, baseline, col, sep);
  weekdays(c, ctx, calendar ? 17 : 2, top ? 0 : 14, calendar ? 4 : 6);
  return true;
}

bool date(Canvas& c, const RenderCtx& ctx) {
  if (!supported(c)) return false;
  const auto& s = *ctx.settings;
  const auto textLine = line(*ctx.font,
    buildDateText(s, ctx.weekday, ctx.mday, ctx.month, ctx.year), true);
  if (textLine.width() > c.width()) return false;
  c.clear();
  const uint32_t col = s.dateColor.valueOr(s.textColor);
  centered(c, *ctx.font, textLine, 0, 52, s.weekdayBar.show ? 12 : 13, col, col);
  weekdays(c, ctx, 2, 14, 6);
  return true;
}

bool volume(Canvas& c, const GfxFont& font, int percent) {
  if (!supported(c)) return false;
  c.clear();
  const int pct = std::clamp(percent, 0, 100);
  constexpr uint32_t color = 0xFFFFFFu;
  constexpr int x = 4;
  // Speaker cone and either sound waves or a mute cross, in physical pixels.
  c.fillRect(x, 6, 4, 4, color);
  c.fillRect(x + 4, 4, 2, 8, color);
  c.fillRect(x + 6, 2, 2, 12, color);
  if (pct) {
    c.fillRect(x + 10, 6, 2, 4, color);
    c.fillRect(x + 12, 2, 2, 2, color);
    c.fillRect(x + 14, 4, 2, 8, color);
    c.fillRect(x + 12, 12, 2, 2, color);
  } else {
    c.drawLine(x + 10, 5, x + 15, 10, color);
    c.drawLine(x + 10, 10, x + 15, 5, color);
  }
  auto label = line(font, formatBattery(pct));
  // The shifted 100% label needs one less column before '%' to fit the panel.
  if (pct == 100) {
    --label.positions.back();
    --label.right;
  }
  centered(c, font, label, x + 17, 34, 13, color, color);
  return true;
}

bool battery(Canvas& c, const RenderCtx& ctx) {
  if (!supported(c)) return false;
  c.clear();
  const int pct = std::clamp<int>(ctx.runtime->batteryPercent, 0, 100);
  const uint32_t col = ctx.runtime->lowBattery || pct < 20 ? 0xFF2000u :
                       pct < 40 ? 0xFFA000u : 0x00E000u;
  // Temporary TC002 workaround: dim green flickers on one physical LED.
  // See the README's display flicker note; keep warning colours unchanged.
  const uint32_t shell = col == 0x00E000u ? 0x007A00u : dim(col, 76);
  c.fillRect(5, 0, 6, 2, shell);
  c.fillRect(5, 2, 2, 2, shell);
  c.fillRect(9, 2, 2, 2, shell);
  c.fillRect(3, 2, 2, 14, shell);
  c.fillRect(11, 2, 2, 14, shell);
  c.fillRect(3, 14, 10, 2, shell);
  const int rows[6][3] = {{5,12,6}, {5,10,6}, {5,8,6}, {5,6,6}, {5,4,6}, {7,2,2}};
  const int full = pct * 6 / 100, remainder = pct * 6 % 100;
  for (int i = 0; i < full; ++i) c.fillRect(rows[i][0], rows[i][1], rows[i][2], 2, col);
  if (full < 6 && remainder)
    c.fillRect(rows[full][0], rows[full][1], rows[full][2], 2, dim(col, 128 + remainder * 127 / 100));
  const uint32_t textCol = ctx.settings->batteryColor.valueOr(ctx.settings->textColor);
  centered(c, *ctx.font, line(*ctx.font, formatBattery(pct)), 17, 34, 13, textCol, textCol);
  return true;
}
}
