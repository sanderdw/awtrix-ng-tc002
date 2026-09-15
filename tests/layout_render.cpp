// Deterministic driver for comparing actual built-in renderers to approved previews.
#include <cstdlib>
#include <iostream>
#include <string>
#include "core/apps/builtin/TimeApp.h"
#include "core/apps/builtin/DateApp.h"
#include "core/apps/builtin/BatteryApp.h"
#include "media/AwtrixFontAdapter.h"

int main(int argc, char** argv) {
  if (argc < 3) return 2;
  awtrix::Canvas canvas(52, 16);
  awtrix::Settings settings;
  awtrix::RuntimeState runtime;
  settings.timeSeparatorMode = awtrix::kSepSteady;
  awtrix::RenderCtx ctx;
  ctx.font = &awtrix::awtrixFont();
  ctx.settings = &settings;
  ctx.runtime = &runtime;
  awtrix::TimeApp time;
  awtrix::DateApp date;
  awtrix::BatteryApp battery;
  awtrix::IApp* app = nullptr;
  const std::string kind = argv[1];
  if (kind == "Time" && argc == 6) {
    ctx.hour = std::atoi(argv[2]); ctx.minute = std::atoi(argv[3]);
    ctx.mday = std::atoi(argv[4]); ctx.weekday = std::atoi(argv[5]);
    app = &time;
  } else if (kind == "Date" && argc == 6) {
    ctx.year = std::atoi(argv[2]); ctx.month = std::atoi(argv[3]);
    ctx.mday = std::atoi(argv[4]); ctx.weekday = std::atoi(argv[5]);
    app = &date;
  } else if (kind == "Battery" && argc == 3) {
    runtime.batteryPercent = std::atoi(argv[2]); app = &battery;
  }
  if (!app || !app->renderNative(canvas, ctx)) return 3;
  std::cout << '[';
  for (std::size_t i = 0; i < canvas.size(); ++i) {
    if (i) std::cout << ',';
    std::cout << canvas.data()[i];
  }
  std::cout << "]\n";
}
