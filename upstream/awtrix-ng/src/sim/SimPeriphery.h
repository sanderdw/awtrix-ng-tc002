#pragma once

#include <cstdint>

#include <functional>

#include "core/sensing/AutoBrightness.h"
#include "core/sensing/BatteryModel.h"
#include "hal/IBoard.h"
#include "persistence/DeviceConfig.h"

namespace awtrix {

class CoreEngine;
class Canvas;

// Host twin of PeripheryService. Same debounce and brightness rules, but deliberately without the
// median filters and smoothing: an injected sensor value must show up on the next tick.
class SimPeriphery {
 public:
  void begin(CoreEngine& engine, IBoard& board, const DeviceConfig& cfg);
  void setButtonHook(std::function<bool(int)> hook) { buttonHook_ = std::move(hook); }
  void tick(int64_t nowMs);
#ifdef AWTRIX_TC002
  void renderControlFeedback(Canvas& canvas, int64_t nowMs) const;
#endif

 private:
  LightConfig lightConfig() const;

  CoreEngine* engine_ = nullptr;
  IBoard* board_ = nullptr;
  const DeviceConfig* cfg_ = nullptr;
  std::function<bool(int)> buttonHook_;
  ButtonState prev_{};
  ButtonState raw_{};
  ButtonState stable_{};
#ifdef AWTRIX_TC002
  void adjustControl(bool brightness, int direction, int64_t nowMs);
  int64_t volumeFeedbackUntilMs_ = 0;
  int64_t controlPressedMs_[2] = {0, 0};
  int64_t controlRepeatMs_[2] = {0, 0};
  bool controlLong_[2] = {false, false};
#endif
  int64_t rawChangeMs_[3] = {0, 0, 0};
  int64_t lastSelectEdgeMs_ = -100000;
  static constexpr long kDebounceMs = 35;
  static constexpr long kDoublePressMs = 300;
  int64_t lastSensorMs_ = -100000;
  int64_t lastLdrMs_ = -100000;
};

}
