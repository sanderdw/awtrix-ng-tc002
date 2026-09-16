#pragma once

#include "LegacyFit.h"
#include "Tc002Layout.h"
#include "core/apps/builtin/BatteryApp.h"
#include "core/apps/builtin/DateApp.h"
#include "core/apps/builtin/HumidityApp.h"
#include "core/apps/builtin/TempApp.h"
#include "core/apps/builtin/TimeApp.h"

namespace awtrix {

// The built-in apps on the 52 by 16 panel: the approved native clock, date and battery layouts
// where they apply, otherwise the upstream 32 by 8 rendering expanded and fitted to the panel.
class Tc002TimeApp : public TimeApp {
 public:
  void render(Canvas& c, const RenderCtx& ctx) override {
    if (tc002layout::time(c, ctx)) return;
    renderLegacyFitted(c, [&](Canvas& legacy) { TimeApp::render(legacy, ctx); });
  }
};

class Tc002DateApp : public DateApp {
 public:
  void render(Canvas& c, const RenderCtx& ctx) override {
    if (tc002layout::date(c, ctx)) return;
    renderLegacyFitted(c, [&](Canvas& legacy) { DateApp::render(legacy, ctx); });
  }
};

class Tc002BatteryApp : public BatteryApp {
 public:
  void render(Canvas& c, const RenderCtx& ctx) override {
    if (tc002layout::battery(c, ctx)) return;
    renderLegacyFitted(c, [&](Canvas& legacy) { BatteryApp::render(legacy, ctx); });
  }
};

class Tc002TempApp : public TempApp {
 public:
  void render(Canvas& c, const RenderCtx& ctx) override {
    renderLegacyFitted(c, [&](Canvas& legacy) { TempApp::render(legacy, ctx); });
  }
};

class Tc002HumidityApp : public HumidityApp {
 public:
  void render(Canvas& c, const RenderCtx& ctx) override {
    renderLegacyFitted(c, [&](Canvas& legacy) { HumidityApp::render(legacy, ctx); });
  }
};

}
