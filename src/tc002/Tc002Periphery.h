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
class Tc002Board;

// TC002 twin of the simulator's periphery service. Same debounce rules, but the physical -/+ rocker
// is reserved for volume (tap) and brightness (hold), and the rotary encoder navigates between apps.
class Tc002Periphery {
 public:
  void begin(CoreEngine& engine, Tc002Board& board, const DeviceConfig& cfg);
  void setButtonHook(std::function<bool(int, bool)> hook) { buttonHook_ = std::move(hook); }
  // Sees every detent, true for "next", whether or not a script or blocked navigation takes it.
  void setRotationHook(std::function<void(bool)> hook) { rotationHook_ = std::move(hook); }
  void tick(int64_t nowMs);
  void renderControlFeedback(Canvas& canvas, int64_t nowMs) const;

 private:
  LightConfig lightConfig() const;
  void adjustControl(bool brightness, int direction, int64_t nowMs);

  CoreEngine* engine_ = nullptr;
  Tc002Board* board_ = nullptr;
  const DeviceConfig* cfg_ = nullptr;
  std::function<bool(int, bool)> buttonHook_;
  std::function<void(bool)> rotationHook_;
  ButtonState prev_{};
  ButtonState raw_{};
  ButtonState stable_{};
  int64_t volumeFeedbackUntilMs_ = 0;
  int64_t controlPressedMs_[2] = {0, 0};
  int64_t controlRepeatMs_[2] = {0, 0};
  bool controlLong_[2] = {false, false};
  int64_t rawChangeMs_[3] = {0, 0, 0};
  int64_t lastSelectEdgeMs_ = -100000;
  static constexpr long kDebounceMs = 35;
  static constexpr long kDoublePressMs = 300;
  int64_t lastSensorMs_ = -100000;
  int64_t lastLdrMs_ = -100000;
};

}
