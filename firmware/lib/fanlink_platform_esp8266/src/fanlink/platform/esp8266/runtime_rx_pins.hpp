#pragma once
#include "fanlink/settings.hpp"
#include "fanlink/platform/esp8266/fan_output.hpp"
#include "fanlink/platform/esp8266/tachometer.hpp"

namespace fanlink::platform::esp8266 {
// Select only at boot, before controller.begin(). UART/safe never initialize a
// PWM output or Tach interrupt. Changing persisted selection requires restart.
class RuntimeFanOutput final : public IFanOutput {
 public:
  RuntimeFanOutput(std::uint32_t hz, bool polarity)
      : pwm_(1, hz, polarity), swapped_(3, hz, polarity) {}
  void select(settings::PinMode mode) { if (!begun_) mode_ = mode; }
  bool begin() override {
    begun_ = true;
    if (mode_ == settings::PinMode::Pwm) output_ = &pwm_;
    if (mode_ == settings::PinMode::PwmSwap) output_ = &swapped_;
    return !output_ || output_->begin();
  }
  void setDuty(std::uint8_t duty) override { if (output_) output_->setDuty(duty); }
  void setFullSpeed() override { if (output_) output_->setFullSpeed(); }
  std::uint8_t appliedDuty() const override { return output_ ? output_->appliedDuty() : 255; }
 private:
  Esp8266FanOutput pwm_, swapped_;
  IFanOutput* output_{nullptr};
  settings::PinMode mode_{settings::PinMode::Safe};
  bool begun_{false};
};
class RuntimeTachometer final : public ITachometer {
 public:
  RuntimeTachometer(std::uint8_t ppr, std::uint32_t timeout, std::uint32_t edge,
                   std::uint32_t maxRpm)
      : pwm_(3, ppr, timeout, edge, maxRpm), swapped_(1, ppr, timeout, edge, maxRpm) {}
  void select(settings::PinMode mode) { if (!begun_) mode_ = mode; }
  bool begin() override {
    begun_ = true;
    if (mode_ == settings::PinMode::Pwm) tach_ = &pwm_;
    if (mode_ == settings::PinMode::PwmSwap) tach_ = &swapped_;
    return !tach_ || tach_->begin();
  }
  TachSample sample(std::uint32_t now) override { return tach_ ? tach_->sample(now) : TachSample{}; }
  TachDiagnostics diagnostics(std::uint32_t now) override {
    return tach_ ? tach_->diagnostics(now) : TachDiagnostics{};
  }
 private:
  Esp8266Tachometer pwm_, swapped_;
  ITachometer* tach_{nullptr};
  settings::PinMode mode_{settings::PinMode::Safe};
  bool begun_{false};
};
}  // namespace fanlink::platform::esp8266
