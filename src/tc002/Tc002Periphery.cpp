#include "Tc002Periphery.h"

#include <algorithm>
#include <cstdlib>

#include "Tc002Board.h"
#include "Tc002Hardware.h"
#include "Tc002Layout.h"
#include "core/Command.h"
#include "core/CoreEngine.h"
#include "media/AwtrixFontAdapter.h"

namespace awtrix {

namespace {
constexpr long kSensorIntervalMs = 2000;
constexpr long kLdrIntervalMs = 100;
}

LightConfig Tc002Periphery::lightConfig() const {
  LightConfig lc;
  lc.factor = cfg_->ldrFactor;
  lc.gamma = cfg_->ldrGamma;
  lc.onGround = cfg_->ldrOnGround;
  lc.minBrightness = cfg_->minBrightness;
  lc.maxBrightness = cfg_->maxBrightness;
  return lc;
}

void Tc002Periphery::begin(CoreEngine& engine, Tc002Board& board, const DeviceConfig& cfg) {
  engine_ = &engine;
  board_ = &board;
  cfg_ = &cfg;
}

void Tc002Periphery::adjustControl(bool brightness, int direction, int64_t nowMs) {
  const auto& s = engine_->state().settings();
  Command command(CommandType::SetSettings);
  if (brightness) {
    // Keep the display visible at the lower end; power-off remains a separate action.
    const int value = std::clamp(s.brightness + direction * 10, 1, 255);
    command.payload = "{\"brightness\":" + std::to_string(value) + ",\"autoBrightness\":false}";
  } else {
    const auto volume = [direction](int value) { return std::to_string(std::clamp(value + direction * 5, 0, 100)); };
    // One physical speaker serves tones, files and radio. Adjust each existing
    // volume by the same step, preserving independently configured levels.
    command.payload = "{\"buzzerVolume\":" + volume(s.buzzerVolume) +
      ",\"mp3Volume\":" + volume(s.mp3Volume) + ",\"radioVolume\":" + volume(s.radioVolume) + "}";
  }
  if (engine_->submit(command)) volumeFeedbackUntilMs_ = brightness ? 0 : nowMs + 1500;
}

void Tc002Periphery::renderControlFeedback(Canvas& canvas, int64_t nowMs) const {
  if (nowMs >= volumeFeedbackUntilMs_) return;
  const auto& s = engine_->state().settings();
  const auto& rt = engine_->state().runtime();
  // Show the active source's level; idle feedback uses the tone volume.
  const int volume = rt.mp3Playing ? s.mp3Volume : rt.radioPlaying ? s.radioVolume : s.buzzerVolume;
  tc002layout::volume(canvas, awtrixFont(FontId::Small), volume);
}

void Tc002Periphery::tick(int64_t nowMs) {
  ButtonState sample{};
  board_->pollButtons(sample);
  const auto debounce = [&](bool raw, bool& lastRaw, int64_t& changedMs, bool& stable) {
    if (raw != lastRaw) {
      lastRaw = raw;
      changedMs = nowMs;
    }
    if (raw != stable && nowMs - changedMs >= kDebounceMs) stable = raw;
  };
  debounce(sample.left, raw_.left, rawChangeMs_[0], stable_.left);
  debounce(sample.select, raw_.select, rawChangeMs_[1], stable_.select);
  debounce(sample.right, raw_.right, rawChangeMs_[2], stable_.right);
  const ButtonState& cur = stable_;
  const bool blocked = engine_->state().settings().blockNavigation;
  const bool sEdge = cur.select && !prev_.select;
  // Rotating the panel by 180 degrees physically swaps left and right, so the user's swap setting
  // has to XOR with it rather than simply override it.
  const bool swapped = cfg_->rotate != cfg_->swapButtons;
  // Scripts get first refusal on every press, button by button; returning true suppresses the
  // built-in action for that button alone.
  // The physical -/+ rocker is reserved for volume/brightness. Delay its short
  // action until release so a brightness hold never also changes the volume.
  const bool held[2] = {cur.left, cur.right};
  const bool wasHeld[2] = {prev_.left, prev_.right};
  for (int i = 0; i < 2; ++i) {
    const int direction = i == 0 ? -1 : 1;
    if (held[i] && !wasHeld[i]) {
      controlPressedMs_[i] = nowMs;
      controlLong_[i] = false;
    }
    if (held[i] && nowMs - controlPressedMs_[i] >= 700 &&
        (!controlLong_[i] || nowMs - controlRepeatMs_[i] >= 200)) {
      controlLong_[i] = true;
      controlRepeatMs_[i] = nowMs;
      adjustControl(true, direction, nowMs);
    }
    if (!held[i] && wasHeld[i] && !controlLong_[i]) adjustControl(false, direction, nowMs);
  }
  const int rotation = board_->takeRotation();
  for (int i = 0; i < std::abs(rotation); ++i) {
    const bool next = (rotation > 0) != swapped;
    // A detent has no held state: report it as a press and its release in one go. Only the press
    // can be consumed.
    const bool handled = buttonHook_ && buttonHook_(next ? 2 : 0, true);
    if (buttonHook_) buttonHook_(next ? 2 : 0, false);
    if (!handled && !blocked)
      engine_->submit(Command(next ? CommandType::NextApp : CommandType::PreviousApp));
  }
  // The hook sees the held state every tick so it can time long presses and the release.
  const bool tookSelect = buttonHook_ && buttonHook_(1, cur.select);
  // One press dismisses the current notification, two inside kDoublePressMs toggle the panel. A
  // consumed press is not remembered, so it can never pair up with the next one.
  if (sEdge && !tookSelect) {
    engine_->submit(Command(CommandType::DismissNotify));
    if (!blocked && nowMs - lastSelectEdgeMs_ <= kDoublePressMs) {
      Command c(CommandType::SetDisplay);
      c.payload = engine_->state().runtime().matrixOff ? "{\"power\":true}" : "{\"power\":false}";
      engine_->submit(c);
    }
    lastSelectEdgeMs_ = nowMs;
  }
  if (cur.left != prev_.left || cur.select != prev_.select || cur.right != prev_.right) {
    engine_->state().runtime().buttons = {cur.left, cur.select, cur.right};
    engine_->state().emit(StateEvent::ButtonsChanged);
  }
  prev_ = cur;

  RuntimeState& rt = engine_->state().runtime();
  const Settings& s = engine_->state().settings();

  if (nowMs - lastLdrMs_ >= kLdrIntervalMs) {
    lastLdrMs_ = nowMs;
    int ldr = board_->readLdrRaw();
    if (ldr < 0) ldr = 0;
    rt.ldrRaw = static_cast<uint16_t>(ldr);
    rt.lightLevel = lightLevelFromRaw(rt.ldrRaw, lightConfig());

    uint8_t bri;
    if (s.autoBrightness && board_->hasLightSensor()) {
      bri = brightnessFromLightLevel(rt.lightLevel, lightConfig());
    } else {
      bri = static_cast<uint8_t>(s.brightness < 0 ? 0 : (s.brightness > 255 ? 255 : s.brightness));
    }
    rt.brightnessActual = bri;
    board_->setBrightness(bri);
  }

  if (nowMs - lastSensorMs_ < kSensorIntervalMs) return;
  lastSensorMs_ = nowMs;

  if (board_->hasBattery()) {
    const int mv = board_->readBatteryMillivolts();
    if (mv >= 0) {
      rt.batteryPinMillivolts = static_cast<uint16_t>(mv);
      rt.batteryVoltage = cellVoltsFromPinMillivolts(mv, cfg_->batteryDividerRatio);
      rt.batteryPercent = tc002::batteryPercent();
      rt.lowBattery = cfg_->lowBatteryThreshold > 0 && rt.batteryPercent < cfg_->lowBatteryThreshold;
    }
  }

  const SensorReading sr = board_->sensors().read();
  if (sr.present) {
    rt.temperatureC = sr.temperatureC + cfg_->tempOffset;
    if (sr.hasHumidity) rt.humidity = sr.humidity + cfg_->humOffset;
    if (sr.hasPressure) rt.pressureHpa = sr.pressureHpa;
  }
}

}
