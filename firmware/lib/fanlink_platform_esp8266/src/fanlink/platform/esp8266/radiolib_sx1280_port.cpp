#include "fanlink/platform/esp8266/radiolib_sx1280_port.hpp"

#include <Arduino.h>

#include "fanlink/protocol.hpp"

namespace fanlink::platform::esp8266 {

#if RADIOLIB_STATIC_ONLY
static_assert(RADIOLIB_STATIC_ARRAY_SIZE >= kAirPacketSize,
              "RadioLib static payload buffer is smaller than a FanLink packet");
// SX128x buffer reads add a two-byte command and one status byte.
static_assert(RADIOLIB_STATIC_SPI_ARRAY_SIZE >= kAirPacketSize + 3U,
              "RadioLib static SPI buffer is smaller than a FanLink packet transaction");
#endif

#if defined(FANLINK_RADIO_USE_DCDC)
static_assert(FANLINK_RADIO_USE_DCDC == 0 || FANLINK_RADIO_USE_DCDC == 1,
              "FANLINK_RADIO_USE_DCDC must be 0 or 1");
#endif

RadioLibSx1280Port::RadioLibSx1280Port(Sx1280PortConfig config)
    : config_(config),
      spiSettings_(config.spiHz, MSBFIRST, SPI_MODE0),
      hal_(SPI, spiSettings_),
      module_(&hal_, config.nssPin, config.dio1Pin, config.resetPin, config.busyPin),
      radio_(&module_) {}

std::array<std::uint8_t, 4> RadioLibSx1280Port::makeSyncWord(std::uint32_t networkId) {
  // Deterministic mixing; this is a network discriminator, not cryptography.
  networkId ^= 0xA53C9E17UL;
  networkId ^= networkId >> 16U;
  networkId *= 0x7FEB352DUL;
  networkId ^= networkId >> 15U;
  std::array<std::uint8_t, 4> sync{
      static_cast<std::uint8_t>(networkId & 0xFFU),
      static_cast<std::uint8_t>((networkId >> 8U) & 0xFFU),
      static_cast<std::uint8_t>((networkId >> 16U) & 0xFFU),
      static_cast<std::uint8_t>((networkId >> 24U) & 0xFFU),
  };
  // Avoid the all-zero pattern.
  if ((sync[0] | sync[1] | sync[2] | sync[3]) == 0U) {
    sync = {0xF1U, 0xA5U, 0x5AU, 0x1FU};
  }
  return sync;
}

RadioResult RadioLibSx1280Port::mapResult(int state) {
  if (state == RADIOLIB_ERR_NONE) {
    return RadioResult::Ok;
  }
  if (state == RADIOLIB_ERR_RX_TIMEOUT) {
    return RadioResult::Timeout;
  }
  if (state == RADIOLIB_ERR_CRC_MISMATCH) {
    return RadioResult::CrcError;
  }
  return RadioResult::IoError;
}

std::int8_t RadioLibSx1280Port::clampRssi(float rssi) {
  if (rssi < -127.0F) {
    return -127;
  }
  if (rssi > 0.0F) {
    return 0;
  }
  return static_cast<std::int8_t>(rssi);
}

bool RadioLibSx1280Port::begin(std::uint32_t networkId) {
  initialized_ = false;

  if (config_.busyTimeoutMs == 0U) {
    lastError_ = RADIOLIB_ERR_INVALID_FUNCTION;
    return false;
  }

#if defined(FANLINK_RADIO_USE_DCDC)
  // The dependency is compiled with this same macro. Fail closed if a caller
  // constructs a port configuration that disagrees with the patched library.
  constexpr bool kCompiledUseDcdc = FANLINK_RADIO_USE_DCDC != 0;
  if (config_.useDcdc != kCompiledUseDcdc) {
    lastError_ = RADIOLIB_ERR_INVALID_FUNCTION;
    return false;
  }
#endif

#if defined(FANLINK_RADIO_BUSY_TIMEOUT_MS)
  if (config_.busyTimeoutMs != FANLINK_RADIO_BUSY_TIMEOUT_MS) {
    lastError_ = RADIOLIB_ERR_INVALID_FUNCTION;
    return false;
  }
#endif

  SPI.begin();

  // Bound every RadioLib SPI/BUSY wait. The dependency integration hook also
  // applies this deadline to SX128x::launchMode(), whose upstream loop is
  // otherwise unbounded.
  module_.spiConfig.timeout = static_cast<RadioLibTime_t>(config_.busyTimeoutMs);

  if (config_.rxEnablePin >= 0 && config_.txEnablePin >= 0) {
    radio_.setRfSwitchPins(static_cast<std::uint32_t>(config_.rxEnablePin),
                           static_cast<std::uint32_t>(config_.txEnablePin));
  }

  // The non-struct overload remains in RadioLib 7.7.1 and is intentionally used
  // here to keep the starter readable. Codex may migrate to ConfigFLRC_t after
  // the first verified build without changing the public radio-port contract.
  lastError_ = radio_.beginFLRC(config_.frequencyMhz,
                                static_cast<std::uint16_t>(config_.bitRateKbps),
                                config_.codingRate, config_.outputPowerDbm,
                                config_.preambleBits, RADIOLIB_SHAPING_0_5);
  if (lastError_ != RADIOLIB_ERR_NONE) {
    return false;
  }

  auto syncWord = makeSyncWord(networkId);
  lastError_ = radio_.setSyncWord(syncWord.data(), syncWord.size());
  if (lastError_ != RADIOLIB_ERR_NONE) {
    return false;
  }

  lastError_ = radio_.setCRC(2U, 0x1D0FU, 0x1021U);
  if (lastError_ != RADIOLIB_ERR_NONE) {
    return false;
  }

  lastError_ = radio_.fixedPacketLengthMode(static_cast<std::uint8_t>(kAirPacketSize));
  initialized_ = lastError_ == RADIOLIB_ERR_NONE;
  return initialized_;
}

RadioResult RadioLibSx1280Port::transmit(const std::uint8_t* data, std::size_t length) {
  if (!initialized_) {
    lastError_ = RADIOLIB_ERR_INVALID_FUNCTION;
    return RadioResult::IoError;
  }
  if (data == nullptr) {
    lastError_ = RADIOLIB_ERR_NULL_POINTER;
    return RadioResult::IoError;
  }
  if (length != kAirPacketSize) {
    lastError_ = length < kAirPacketSize ? RADIOLIB_ERR_PACKET_TOO_SHORT
                                        : RADIOLIB_ERR_PACKET_TOO_LONG;
    return RadioResult::IoError;
  }
  lastError_ = radio_.transmit(data, length);
  return mapResult(lastError_);
}

RadioRxResult RadioLibSx1280Port::receive(std::uint8_t* data, std::size_t capacity,
                                         std::uint32_t timeoutMs) {
  if (!initialized_) {
    lastError_ = RADIOLIB_ERR_INVALID_FUNCTION;
    return RadioRxResult{RadioResult::IoError, 0U, -127, lastError_};
  }
  if (data == nullptr) {
    lastError_ = RADIOLIB_ERR_NULL_POINTER;
    return RadioRxResult{RadioResult::IoError, 0U, -127, lastError_};
  }
  if (capacity != kAirPacketSize) {
    lastError_ = capacity < kAirPacketSize ? RADIOLIB_ERR_PACKET_TOO_SHORT
                                          : RADIOLIB_ERR_PACKET_TOO_LONG;
    return RadioRxResult{RadioResult::IoError, 0U, -127, lastError_};
  }

  // The radio is configured for a fixed kAirPacketSize payload in begin().
  // SX128x's blocking receive overload documents timeout in microseconds.
  const auto timeoutUs = static_cast<RadioLibTime_t>(timeoutMs) * 1000U;
  lastError_ = radio_.receive(data, capacity, timeoutUs);
  const auto result = mapResult(lastError_);
  if (result != RadioResult::Ok) {
    return RadioRxResult{result, 0U, -127, lastError_};
  }

  return RadioRxResult{RadioResult::Ok, kAirPacketSize, clampRssi(radio_.getRSSI()), lastError_};
}

}  // namespace fanlink::platform::esp8266
