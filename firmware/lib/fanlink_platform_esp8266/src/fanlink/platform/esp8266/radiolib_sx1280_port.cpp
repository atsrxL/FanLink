#include "fanlink/platform/esp8266/radiolib_sx1280_port.hpp"

#include <Arduino.h>
#include <limits>

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

void RadioLibSx1280Port::trace(RadioTraceEvent event, std::uint32_t value, int result,
                              std::uint8_t valid) {
  const auto stamp = static_cast<std::uint32_t>(hal_.micros());
  // Sequential software GPIO samples. They are not simultaneous or IRQ edges.
  const auto pins = static_cast<std::uint8_t>(
      (hal_.digitalRead(static_cast<std::uint32_t>(config_.dio1Pin)) ? 1U : 0U) |
      (hal_.digitalRead(static_cast<std::uint32_t>(config_.busyPin)) ? 2U : 0U));
  const auto code = result < INT16_MIN ? INT16_MIN : (result > INT16_MAX ? INT16_MAX : result);
  trace_.record({stamp, value, static_cast<std::int16_t>(code), event, pins,
      static_cast<std::uint8_t>(valid | radio_trace_valid::kTime | radio_trace_valid::kPins)});
}

void RadioLibSx1280Port::traceContext(RadioTraceEvent event, const std::uint8_t* data) {
  const auto word = (static_cast<std::uint32_t>(data[3]) << 24U) |
      (static_cast<std::uint32_t>(data[10]) << 16U) |
      (static_cast<std::uint32_t>(data[9]) << 8U) | data[8];
  trace(event, word, 0, radio_trace_valid::kValue);
}

int RadioLibSx1280Port::clearReceiveIrq() {
  trace(RadioTraceEvent::ClearBegin, RADIOLIB_SX128X_IRQ_ALL, 0, radio_trace_valid::kValue);
  const int state = radio_.clearIrqFlags(RADIOLIB_SX128X_IRQ_ALL);
  trace(RadioTraceEvent::ClearEnd, 0, state, radio_trace_valid::kResult);
  return state;
}

void RadioLibSx1280Port::idleRfSwitch() {
  if (config_.rxEnablePin >= 0 && config_.txEnablePin >= 0) {
    // Match RadioLib's two-pin RF switch table. This must work even when SPI or
    // BUSY is broken, so cleanup must not issue another radio command.
    digitalWrite(config_.txEnablePin, LOW);
    digitalWrite(config_.rxEnablePin, LOW);
  }
}

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
  receiveArmed_ = false;
  // begin() is also the runtime radio recovery entry point. Release any old
  // front-end state before resetting/reconfiguring the chip, without resetting
  // the caller's controller state or waiting for an SPI cleanup transaction.
  idleRfSwitch();

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
    idleRfSwitch();
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

  // Preserve the deployed RadioLib 7.7.1 FLRC encoding: argument 2 maps to
  // packet-parameter 0x20 (three hardware CRC bytes in FLRC). The seed is still
  // configured; the polynomial argument writes only the GFSK polynomial
  // registers and does not change FLRC's fixed hardware polynomial.
  // FanLink's separate 16-bit application CRC remains unchanged.
  lastError_ = radio_.setCRC(2U, 0x1D0FU, 0x1021U);
  if (lastError_ != RADIOLIB_ERR_NONE) {
    return false;
  }

  lastError_ = radio_.fixedPacketLengthMode(static_cast<std::uint8_t>(kAirPacketSize));
  initialized_ = lastError_ == RADIOLIB_ERR_NONE;
  return initialized_;
}

RadioResult RadioLibSx1280Port::transmit(const std::uint8_t* data, std::size_t length) {
  receiveArmed_ = false;
  idleRfSwitch();
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
  traceContext(RadioTraceEvent::TxContext, data);
  trace(RadioTraceEvent::TxBegin);
  lastError_ = radio_.transmit(data, length);
  idleRfSwitch();
  trace(RadioTraceEvent::TxEnd, 0, lastError_, radio_trace_valid::kResult);
  if (lastError_ != RADIOLIB_ERR_NONE) trace_.finish(RadioTraceKind::Io);
  return mapResult(lastError_);
}

int RadioLibSx1280Port::armReceive() {
  trace(RadioTraceEvent::ArmBegin);
  // SX1280 single RX, no hardware timeout. Software can return to the safety
  // loop without interrupting reception. Do NOT use RxContinuous (0xFFFF):
  // Semtech's all-modem congested-traffic erratum recommends SetRx(0).
  RadioModeConfig_t receiveConfig{};
  receiveConfig.receive.timeout = RADIOLIB_SX128X_RX_TIMEOUT_NONE;
  receiveConfig.receive.irqFlags = RADIOLIB_IRQ_RX_DEFAULT_FLAGS;
  receiveConfig.receive.irqMask = RADIOLIB_IRQ_RX_DEFAULT_MASK;
  receiveConfig.receive.len = kAirPacketSize;
  int state = radio_.stageMode(RADIOLIB_RADIO_MODE_RX, &receiveConfig);
  // stageMode includes an IRQ clear; its return is not IRQ-zero readback.
  trace(RadioTraceEvent::ArmStageEnd, 0, state, radio_trace_valid::kResult);
  if (state == RADIOLIB_ERR_NONE) {
    // RadioLib's generic RX flags omit FLRC SyncWordError. A single RX can
    // end on that event without RxDone. Enable and route it BEFORE SetRx so
    // receiveArmed_ cannot hide a stopped receiver indefinitely. This command
    // does not clear IRQ/FIFO; stageMode has already cleared the previous RX.
    constexpr std::uint16_t flags = RADIOLIB_SX128X_IRQ_RX_DONE |
        RADIOLIB_SX128X_IRQ_RX_TX_TIMEOUT | RADIOLIB_SX128X_IRQ_CRC_ERROR |
        RADIOLIB_SX128X_IRQ_HEADER_VALID | RADIOLIB_SX128X_IRQ_HEADER_ERROR |
        RADIOLIB_SX128X_IRQ_SYNC_WORD_ERROR;
    constexpr std::uint16_t mask = RADIOLIB_SX128X_IRQ_RX_DONE |
        RADIOLIB_SX128X_IRQ_RX_TX_TIMEOUT | RADIOLIB_SX128X_IRQ_SYNC_WORD_ERROR;
    const std::uint8_t mapping[]{static_cast<std::uint8_t>(flags >> 8U),
        static_cast<std::uint8_t>(flags), static_cast<std::uint8_t>(mask >> 8U),
        static_cast<std::uint8_t>(mask), 0U, 0U, 0U, 0U};
    state = module_.SPIwriteStream(RADIOLIB_SX128X_CMD_SET_DIO_IRQ_PARAMS,
                                   mapping, sizeof(mapping));
  }
  if (state == RADIOLIB_ERR_NONE) {
    trace(RadioTraceEvent::ArmLaunchBegin);
    state = radio_.launchMode();
  }
  receiveArmed_ = state == RADIOLIB_ERR_NONE;
  if (receiveArmed_) {
    // Completion of SetRx, not the physical instant the demodulator is ready.
    receiveStartedUs_ = static_cast<std::uint32_t>(hal_.micros());
  } else {
    idleRfSwitch();
  }
  trace(RadioTraceEvent::ArmEnd, 0, state, radio_trace_valid::kResult);
  return state;
}

RadioRxEvidence RadioLibSx1280Port::receiveEvidence() const {
  RadioRxEvidence evidence;
  if (receiveArmed_) {
    evidence.receiveStarted = true;
    evidence.receiveStartedUs = receiveStartedUs_;
  }
  return evidence;
}

RadioRxResult RadioLibSx1280Port::failReceive(int error, const RadioRxEvidence& evidence) {
  receiveArmed_ = false;
  idleRfSwitch();
  lastError_ = error;
  trace(RadioTraceEvent::FaultCleanup, 0, error, radio_trace_valid::kResult);
  trace_.finish(RadioTraceKind::Io);
  return RadioRxResult{RadioResult::IoError, 0U, -127, lastError_, false, 0U, evidence};
}

RadioRxResult RadioLibSx1280Port::receive(std::uint8_t* data, std::size_t capacity,
                                         std::uint32_t timeoutMs) {
  if (timeoutMs > std::numeric_limits<std::uint32_t>::max() / 1000U) {
    return failReceive(RADIOLIB_ERR_INVALID_FUNCTION, receiveEvidence());
  }
  return receiveForUs(data, capacity, timeoutMs * 1000U);
}

RadioRxResult RadioLibSx1280Port::receiveForUs(std::uint8_t* data, std::size_t capacity,
                                              std::uint32_t timeoutUs) {
  auto evidence = receiveEvidence();
  if (!initialized_) {
    return failReceive(RADIOLIB_ERR_INVALID_FUNCTION, evidence);
  }
  if (data == nullptr) {
    return failReceive(RADIOLIB_ERR_NULL_POINTER, evidence);
  }
  if (capacity != kAirPacketSize) {
    return failReceive(capacity < kAirPacketSize ? RADIOLIB_ERR_PACKET_TOO_SHORT
                                                : RADIOLIB_ERR_PACKET_TOO_LONG, evidence);
  }
  const auto startedUs = static_cast<std::uint32_t>(hal_.micros());
  if (!receiveArmed_) {
    lastError_ = armReceive();
    if (lastError_ != RADIOLIB_ERR_NONE) {
      return failReceive(lastError_, evidence);
    }
    evidence = receiveEvidence();
  }
  while (!hal_.digitalRead(static_cast<std::uint32_t>(config_.dio1Pin))) {
    if (static_cast<std::uint32_t>(hal_.micros() - startedUs) >= timeoutUs) {
      // Preserve RX/FIFO/IRQ across the caller's watchdog, LED and yield work.
      // A later transmit will explicitly abandon this old receive operation.
      lastError_ = RADIOLIB_ERR_RX_TIMEOUT;
      return RadioRxResult{RadioResult::Timeout, 0U, -127, lastError_, false, 0U, evidence};
    }
    hal_.yield();
  }

  // Software observation, not the physical DIO edge time. Capture before SPI
  // reads/cleanup/rearm; those can consume the remaining reply budget.
  const auto observedUs = static_cast<std::uint32_t>(hal_.micros());
  evidence.eventObserved = true;
  evidence.eventObservedUs = observedUs;
  trace(RadioTraceEvent::DioObserved, observedUs, 0, radio_trace_valid::kValue);
  receiveArmed_ = false;
  // Check the SPI return codes as well as IRQ/length. RadioLib's convenience
  // readData() repeats unchecked IRQ/length reads and can truncate a frame.
  // Consume exactly the metadata checked here instead of reading it twice.
  lastError_ = radio_.standby();
  trace(RadioTraceEvent::StandbyEnd, 0, lastError_, radio_trace_valid::kResult);
  if (lastError_ != RADIOLIB_ERR_NONE) {
    return failReceive(lastError_, evidence);
  }
  std::uint8_t irqBytes[2]{};
  lastError_ = module_.SPIreadStream(RADIOLIB_SX128X_CMD_GET_IRQ_STATUS, irqBytes, 2U);
  trace(RadioTraceEvent::IrqRead, (irqBytes[0] << 8U) | irqBytes[1], lastError_,
      radio_trace_valid::kResult | (lastError_ == RADIOLIB_ERR_NONE ? radio_trace_valid::kValue : 0U));
  if (lastError_ != RADIOLIB_ERR_NONE) {
    return failReceive(lastError_, evidence);
  }
  const auto irq = static_cast<std::uint16_t>((irqBytes[0] << 8U) | irqBytes[1]);
  evidence.irqValid = true;
  evidence.irq = irq;
  if ((irq & RADIOLIB_SX128X_IRQ_CRC_ERROR) != 0U) {
    evidence.errorFlags |= radio_rx_error::kIrqCrc;
  }
  if ((irq & RADIOLIB_SX128X_IRQ_SYNC_WORD_ERROR) != 0U) {
    evidence.errorFlags |= radio_rx_error::kIrqSync;
  }
  if ((irq & RADIOLIB_SX128X_IRQ_HEADER_ERROR) != 0U &&
      (irq & RADIOLIB_SX128X_IRQ_HEADER_VALID) == 0U) {
    evidence.errorFlags |= radio_rx_error::kHeader;
  }
  if ((irq & RADIOLIB_SX128X_IRQ_RX_DONE) == 0U) {
    if ((irq & RADIOLIB_SX128X_IRQ_SYNC_WORD_ERROR) != 0U) {
      evidence.errorFlags |= radio_rx_error::kSyncWithoutRxDone;
      lastError_ = clearReceiveIrq();
      if (lastError_ != RADIOLIB_ERR_NONE) return failReceive(lastError_, evidence);
      lastError_ = armReceive();
      if (lastError_ != RADIOLIB_ERR_NONE) return failReceive(lastError_, evidence);
      lastError_ = RADIOLIB_ERR_CRC_MISMATCH;
      trace(RadioTraceEvent::RxResult, 0, lastError_, radio_trace_valid::kResult);
      trace_.finish(RadioTraceKind::Crc);
      // A rejected RF frame, not an SPI hardware failure or a received packet.
      return RadioRxResult{RadioResult::CrcError, 0U, -127, lastError_, false, 0U, evidence};
    }
    return failReceive(RADIOLIB_ERR_UNKNOWN, evidence);
  }
  std::uint8_t bufferStatus[2]{};
  lastError_ = module_.SPIreadStream(RADIOLIB_SX128X_CMD_GET_RX_BUFFER_STATUS,
                                    bufferStatus, 2U);
  trace(RadioTraceEvent::BufferRead, bufferStatus[0] | (bufferStatus[1] << 8U), lastError_,
      radio_trace_valid::kResult | (lastError_ == RADIOLIB_ERR_NONE ? radio_trace_valid::kValue : 0U));
  if (lastError_ != RADIOLIB_ERR_NONE) {
    return failReceive(lastError_, evidence);
  }
  if (bufferStatus[0] != kAirPacketSize) {
    return failReceive(bufferStatus[0] < kAirPacketSize ? RADIOLIB_ERR_PACKET_TOO_SHORT
                                                      : RADIOLIB_ERR_PACKET_TOO_LONG, evidence);
  }
  // FLRC packet status byte 2 also carries CRC errors (Semtech erratum 16.3).
  // Read RSSI/status before rearming, and reject failed SPI reads explicitly.
  std::uint8_t packetStatus[5]{};
  lastError_ = module_.SPIreadStream(RADIOLIB_SX128X_CMD_GET_PACKET_STATUS,
                                    packetStatus, 5U);
  trace(RadioTraceEvent::PacketStatusRead, static_cast<std::uint32_t>(packetStatus[1]) |
      (static_cast<std::uint32_t>(packetStatus[2]) << 8U) |
      (static_cast<std::uint32_t>(packetStatus[3]) << 16U) |
      (static_cast<std::uint32_t>(packetStatus[4]) << 24U), lastError_,
      radio_trace_valid::kResult | (lastError_ == RADIOLIB_ERR_NONE ? radio_trace_valid::kValue : 0U));
  if (lastError_ != RADIOLIB_ERR_NONE) {
    return failReceive(lastError_, evidence);
  }
  evidence.packetStatusValid = true;
  evidence.packetStatusError = packetStatus[2];
  evidence.packetStatusSync = packetStatus[4];
  // This sample belongs only to this rejected/untrusted RF event. Never expose
  // it as accepted telemetry; IoError cleanup paths preserve it for diagnosis.
  evidence.errorRssiDbm = clampRssi(-0.5F * packetStatus[1]);
  if ((packetStatus[2] & RADIOLIB_SX128X_PACKET_STATUS_CRC_ERROR) != 0U) {
    evidence.errorFlags |= radio_rx_error::kPacketCrc;
  }
  if ((packetStatus[2] & (RADIOLIB_SX128X_PACKET_STATUS_SYNC_ERROR |
                         RADIOLIB_SX128X_PACKET_STATUS_LENGTH_ERROR |
                         RADIOLIB_SX128X_PACKET_STATUS_ABORT_ERROR)) != 0U) {
    return failReceive(RADIOLIB_ERR_UNKNOWN, evidence);
  }
  const bool crcError = (irq & (RADIOLIB_SX128X_IRQ_CRC_ERROR |
                               RADIOLIB_SX128X_IRQ_SYNC_WORD_ERROR)) != 0U ||
      ((irq & RADIOLIB_SX128X_IRQ_HEADER_ERROR) != 0U &&
       (irq & RADIOLIB_SX128X_IRQ_HEADER_VALID) == 0U) ||
      (packetStatus[2] & RADIOLIB_SX128X_PACKET_STATUS_CRC_ERROR) != 0U;
  const auto result = crcError ? RadioResult::CrcError : RadioResult::Ok;
  const int packetError = crcError ? RADIOLIB_ERR_CRC_MISMATCH : RADIOLIB_ERR_NONE;
  std::int8_t rssi = -127;
  if (!crcError) {
    const std::uint8_t readCommand[]{RADIOLIB_SX128X_CMD_READ_BUFFER, bufferStatus[1]};
    lastError_ = module_.SPIreadStream(readCommand, sizeof(readCommand), data, capacity);
    trace(RadioTraceEvent::FifoRead, 0, lastError_, radio_trace_valid::kResult);
    if (lastError_ != RADIOLIB_ERR_NONE) {
      return failReceive(lastError_, evidence);
    }
    // FLRC RSSI byte 1 is in negative half-dBm steps.
    rssi = clampRssi(-0.5F * packetStatus[1]);
    traceContext(RadioTraceEvent::RxContext, data);
  }
  lastError_ = clearReceiveIrq();
  if (lastError_ != RADIOLIB_ERR_NONE) {
    return failReceive(lastError_, evidence);
  }
  lastError_ = armReceive();
  if (lastError_ != RADIOLIB_ERR_NONE) {
    return failReceive(lastError_, evidence);
  }
  lastError_ = packetError;
  trace(RadioTraceEvent::RxResult, 0, lastError_, radio_trace_valid::kResult);
  trace_.finish(result == RadioResult::Ok ? RadioTraceKind::Good : RadioTraceKind::Crc);
  if (result == RadioResult::Ok) evidence.errorRssiDbm = -127;

  return RadioRxResult{result, result == RadioResult::Ok ? kAirPacketSize : 0U,
                       rssi, lastError_, true, observedUs, evidence};
}

}  // namespace fanlink::platform::esp8266
