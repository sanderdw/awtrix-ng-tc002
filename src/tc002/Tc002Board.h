#pragma once

#include "Tc002Audio.h"
#include "Tc002Hardware.h"
#include "sim/SimBoard.h"

namespace awtrix {

// The TC002 has no temperature, humidity or pressure sensor.
class Tc002Sensors : public SimSensors {
 public:
  bool hasSensor() const override { return false; }
  SensorReading read() override {
    SensorReading r;
    r.present = false;
    r.hasHumidity = false;
    r.temperatureC = temperatureC;
    r.humidity = humidity;
    return r;
  }
};

// The simulator board with the clock's real panel, buttons, rotary encoder, battery gauge and
// speaker behind it. Without --hardware it behaves like the plain simulator, which is what the
// host tests run against.
class Tc002Board : public SimBoard {
 public:
  const char* name() const override { return "Ulanzi TC002"; }
  void begin() override {
    hardware_.begin();
    audio_.begin();
  }
  void setMatrixLayout(const MatrixLayout& layout) override {
    layout_ = layout;
    layout_.panelWidth = 52;
    layout_.panels = 1;
    SimBoard::setMatrixLayout(layout_);
  }
  bool hasBattery() const override { return tc002::hardwareEnabled(); }
  bool hasLightSensor() const override { return false; }
  int readBatteryMillivolts() override { return hardware_.batteryMillivolts(); }
  void pollButtons(ButtonState& out) override {
    SimBoard::pollButtons(out);
    if (tc002::hardwareEnabled()) {
      int rotation = 0;
      hardware_.poll(out.left, out.select, out.right, rotation);
      pendingRotation += rotation;
    }
  }
  // Consume rotary detents separately from held button states.
  int takeRotation() {
    const int value = pendingRotation;
    pendingRotation = 0;
    return value;
  }
  sound::IToneSink* toneSink() override { return audio_.available() ? &audio_ : nullptr; }
  ISensorBus& sensors() override { return sensors_; }

  tc002::Audio& audio() { return audio_; }
  void showHardware(const Canvas& canvas) {
    hardware_.show(canvas.data(), layout_.mirror, layout_.rotate180);
  }

  int pendingRotation = 0;

 private:
  tc002::Hardware hardware_;
  tc002::Audio audio_;
  Tc002Sensors sensors_;
  MatrixLayout layout_;
};

}
