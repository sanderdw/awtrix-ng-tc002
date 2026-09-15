#pragma once

namespace awtrix {
class Canvas;
struct RenderCtx;
namespace tc002layout {
// Return false for layouts outside the approved native presentation, so the
// caller can retain the existing renderer for those modes and formats.
bool time(Canvas& c, const RenderCtx& ctx);
bool date(Canvas& c, const RenderCtx& ctx);
bool battery(Canvas& c, const RenderCtx& ctx);
}
}
