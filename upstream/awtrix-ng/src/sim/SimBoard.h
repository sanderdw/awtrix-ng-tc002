#pragma once

#include <cstdint>

#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include "core/render/MatrixLayout.h"
#include "core/sound/Rtttl.h"
#include "hal/IBoard.h"
#include "sim/SimStore.h"
#include "system/Log.h"
#ifdef AWTRIX_TC002
#include "Tc002Hardware.h"
#include "Tc002Audio.h"
#endif

namespace awtrix {

// No buzzer on the host: logged and reported finished at once, so apps that wait never block.
// The lookup is real, or the simulator would answer "no melody called x" with a sound.
class SimToneSink : public sound::IToneSink {
 public:
  void begin() override {}
  void setVolume(uint8_t percent) override { volume_ = percent; }
  bool playMelodyFile(const std::string& name) override {
    if (!rtttl::validName(name)) return false;
    const std::string path = sim::hostPath("/MELODIES/" + name + ".txt");
    if (!std::filesystem::exists(std::filesystem::u8path(path))) return false;
    logf("sim sound: melody '%s'", name.c_str());
    return true;
  }
  bool playRtttl(const std::string& rtttl) override {
    logf("sim sound: rtttl '%.60s%s'", rtttl.c_str(), rtttl.size() > 60 ? "..." : "");
    return true;
  }
  void stop() override {}
  void tick() override {}
  bool isPlaying() const override { return false; }

 private:
  uint8_t volume_ = 80;
};

// Always "present" with whatever values the /sim/sensors route last wrote into the public fields.
class SimSensors : public ISensorBus {
 public:
  void begin() override {}
  bool hasSensor() const override {
#ifdef AWTRIX_TC002
    return false;
#else
    return true;
#endif
  }
  SensorReading read() override {
    SensorReading r;
    r.present = hasSensor();
    r.hasHumidity = hasSensor();
    r.temperatureC = temperatureC;
    r.humidity = humidity;
    return r;
  }
  const char* sensorName() const override { return "SIM"; }

  float temperatureC = 21.5f;
  float humidity = 42.0f;
};

class SimBoard : public IBoard {
 public:
  const char* name() const override {
#ifdef AWTRIX_TC002
    return "Ulanzi TC002";
#else
    return "Simulator";
#endif
  }
  int matrixWidth() const override { return layout_.width(); }
  int matrixHeight() const override { return layout_.height(); }

  void begin() override {
#ifdef AWTRIX_TC002
    hardware_.begin();
    audio_.begin();
#endif
  }
  // On the device the colour grade is applied inside the LED driver, so grade here too before the
  // frame is handed on; graded_ is a scratch canvas kept only while a non-identity grade is set.
  void show(const Canvas& canvas) override {
    ++framesShown_;
    if (!onShow) return;
    if (grade_.isIdentity()) {
      onShow(canvas, brightness_);
      return;
    }
    if (!graded_ || graded_->width() != canvas.width() || graded_->height() != canvas.height())
      graded_.reset(new Canvas(canvas.width(), canvas.height()));
    grade_.apply(canvas, *graded_);
    onShow(*graded_, brightness_);
  }
  void setBrightness(uint8_t brightness) override {
    brightness_ = brightness;
    applyGrade();
  }
  void setMatrixLayout(const MatrixLayout& layout) override {
    layout_ = layout;
#ifdef AWTRIX_TC002
    layout_.panelWidth = 52;
    layout_.panels = 1;
#endif
  }
  void applyColorGrade(const render::GradeParams& grade) override {
    baseGrade_ = grade;
    applyGrade();
  }

  // Same as the LED driver: brightness is folded into the grade, so a consumer gets a finished
  // frame rather than one it has to dim itself.
  void applyGrade() {
    render::GradeParams p = baseGrade_;
    p.brightness = brightness_;
    grade_.setParams(p);
  }

  bool hasBattery() const override {
#ifdef AWTRIX_TC002
    return tc002::hardwareEnabled();
#else
    return true;
#endif
  }
  bool hasLightSensor() const override {
#ifdef AWTRIX_TC002
    return false;
#else
    return true;
#endif
  }
  int readBatteryMillivolts() override {
#ifdef AWTRIX_TC002
    return hardware_.batteryMillivolts();
#else
    return batteryPinMillivolts;
#endif
  }
  int readLdrRaw() override { return ldrRaw; }
  // Presses are "held until a deadline" that the /sim/button route sets, which is why they have to
  // outlast the 35 ms debounce to register at all.
  void pollButtons(ButtonState& out) override {
    const int64_t now = nowMs_;
    out.left = now < leftUntilMs;
    out.select = now < selectUntilMs;
    out.right = now < rightUntilMs;
#ifdef AWTRIX_TC002
    if (tc002::hardwareEnabled()) {
      int rotation=0;
      hardware_.poll(out.left, out.select, out.right, rotation);
      pendingRotation += rotation;
    }
#endif
  }

  int takeRotation() override {
    const int value = pendingRotation;
    pendingRotation = 0;
    return value;
  }
  int pendingRotation = 0;

  sound::IToneSink* toneSink() override {
#ifdef AWTRIX_TC002
    return audio_.available() ? &audio_ : nullptr;
#else
    return &tone_;
#endif
  }
  // No DFPlayer is simulated: a fake that always says yes would teach the resolution order a lie.
  sound::ITrackSink* trackSink() override { return nullptr; }
  ISensorBus& sensors() override { return sensors_; }

  void setNow(int64_t nowMs) { nowMs_ = nowMs; }
  int64_t now() const { return nowMs_; }
  SimSensors& simSensors() { return sensors_; }
  uint8_t brightness() const { return brightness_; }
#ifdef AWTRIX_TC002
  tc002::Audio& audio() { return audio_; }
  void showHardware(const Canvas& canvas) {
    hardware_.show(canvas.data(), layout_.mirror, layout_.rotate180);
  }
#endif

  // Where finished frames go: the terminal renderer or a test hooks this. Unset means nothing is
  // drawn anywhere, which is the normal headless case.
  std::function<void(const Canvas&, uint8_t)> onShow;

  int64_t leftUntilMs = 0;
  int64_t selectUntilMs = 0;
  int64_t rightUntilMs = 0;
  // Same units the ESP32 board reads: raw ADC counts and millivolts at the divider pin.
  int ldrRaw = 1200;
  int batteryPinMillivolts = 2290;

 private:
#ifdef AWTRIX_TC002
  tc002::Hardware hardware_;
  tc002::Audio audio_;
#endif
  MatrixLayout layout_;
  render::GradeParams baseGrade_;
  render::ColorGrade grade_;
  std::unique_ptr<Canvas> graded_;
  SimToneSink tone_;
  SimSensors sensors_;
  uint8_t brightness_ = 120;
  int64_t nowMs_ = 0;
  unsigned long framesShown_ = 0;
};

}
