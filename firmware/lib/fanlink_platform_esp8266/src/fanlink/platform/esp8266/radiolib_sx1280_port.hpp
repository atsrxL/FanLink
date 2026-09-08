#pragma once

#include <RadioLib.h>
#include <SPI.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "fanlink/interfaces.hpp"
#include "fanlink/radio_trace.hpp"

namespace fanlink::platform::esp8266 {

struct Sx1280PortConfig {
  int nssPin{15};
  int dio1Pin{4};
  int resetPin{2};
  int busyPin{5};
  int rxEnablePin{-1};
  int txEnablePin{-1};
  bool useDcdc{false};
  std::uint32_t spiHz{2000000UL};
  float frequencyMhz{2405.0F};
  float bitRateKbps{650.0F};
  std::uint8_t codingRate{3U};
  std::int8_t outputPowerDbm{-18};
  std::uint16_t preambleBits{16U};
  std::uint32_t busyTimeoutMs{25U};
};

class RadioLibSx1280Port final : public IRadioPort {
 public:
  explicit RadioLibSx1280Port(Sx1280PortConfig config);

  bool begin(std::uint32_t networkId) override;
  // Boot-only selection: begin/recovery will consistently use this value.
  // Board-specific PA validation belongs to SettingsService.
  bool setOutputPower(std::int8_t dbm) {
    if (initialized_ || dbm < -18 || dbm > 13) return false;
    config_.outputPowerDbm = dbm;
    return true;
  }
  RadioResult transmit(const std::uint8_t* data, std::size_t length) override;
  RadioRxResult receive(std::uint8_t* data, std::size_t capacity,
                        std::uint32_t timeoutMs) override;
  RadioRxResult receiveForUs(std::uint8_t* data, std::size_t capacity,
                             std::uint32_t timeoutUs) override;
  int lastError() const override { return lastError_; }
  bool peekRadioTrace(RadioTraceRecord& record) const { return trace_.peek(record); }
  void popRadioTrace() { trace_.pop(); }
  // Best-effort sequential software gate, not an atomic IRQ/packet boundary.
  bool canEmitDeferredTrace() {
    return initialized_ && receiveArmed_ &&
        !hal_.digitalRead(static_cast<std::uint32_t>(config_.dio1Pin)) &&
        !hal_.digitalRead(static_cast<std::uint32_t>(config_.busyPin));
  }

 private:
  void idleRfSwitch();
  void trace(RadioTraceEvent event, std::uint32_t value = 0U, int result = 0,
             std::uint8_t valid = 0U);
  void traceContext(RadioTraceEvent event, const std::uint8_t* data);
  int clearReceiveIrq();
  int armReceive();
  RadioRxEvidence receiveEvidence() const;
  RadioRxResult failReceive(int error, const RadioRxEvidence& evidence);
  static std::array<std::uint8_t, 4> makeSyncWord(std::uint32_t networkId);
  static RadioResult mapResult(int state);
  static std::int8_t clampRssi(float rssi);

  Sx1280PortConfig config_;
  SPISettings spiSettings_;
  ArduinoHal hal_;
  Module module_;
  SX1280 radio_;
  int lastError_{RADIOLIB_ERR_NONE};
  bool initialized_{false};
  bool receiveArmed_{false};
  std::uint32_t receiveStartedUs_{0};
  RadioTraceRing trace_{};
};

}  // namespace fanlink::platform::esp8266
