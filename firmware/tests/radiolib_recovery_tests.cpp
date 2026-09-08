// Real RadioLib and the production adapter, with only Arduino/SPI/GPIO faked.
// These tests prove control-flow recovery/cleanup, not physical RF behavior.
#include "modules/SX128x/SX1280.h"
#include "Arduino.h"
#include "SPI.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

namespace {

class ProbeHal : public RadioLibHal {
 public:
  ProbeHal() : RadioLibHal(0, 1, 0, 1, 2, 3) {}
  std::array<uint8_t, 65536> registers{};
  std::array<uint8_t, 3> modulation{};
  std::array<uint8_t, 7> packetParameters{};
  std::array<uint8_t, 2> transmitParameters{};
  uint8_t regulator{0};
  std::array<uint32_t, 17> pins{};
  std::array<unsigned, 17> pinWrites{};
  bool requireExternalRfSwitch{true};
  RadioLibTime_t now{0};
  RadioLibTime_t bootBusyAt{0};
  RadioLibTime_t bootReadyAt{0};
  uint8_t packetType{0};
  uint32_t frequency{0};
  uint16_t irq{0};
  int resets{0};
  int modemWrites{0};
  int frequencyWrites{0};
  int standbyWrites{0};
  bool busy{false};
  bool permanentlyBusy{false};
  bool missingChip{false};
  bool wrongVersion{false};
  bool failSetTx{false};
  bool failSetRx{false};
  bool withholdTxDone{false};
  bool failClearAfterTx{false};
  bool clearIrqFault{false};
  bool receiving{false};
  uint16_t rxPeriod{0};
  unsigned rxStarts{0};
  std::array<uint8_t, 30> rxData{};
  uint8_t rxLength{30};
  uint16_t scheduledIrq{0};
  RadioLibTime_t scheduledAt{0};
  uint8_t failReadCommand{0};
  uint8_t packetErrorStatus{0};
  uint8_t packetSyncStatus{0};
  uint8_t rxOffset{0};
  std::array<unsigned, 256> commandCounts{};
  unsigned failCommandOccurrence{0};
  uint16_t dio1Mask{0};
  uint16_t enabledIrqs{0};
  bool requireSyncMapping{false};
  RadioLibTime_t yieldUs{100};
  uint8_t delayedCommand{0};
  RadioLibTime_t commandDelayUs{0};
  bool spuriousDioHigh{false};
  bool busyOnIrqRead{false};
  bool expectIdleOnIrqRead{false};

  bool injectPacket(uint16_t packetIrq = RADIOLIB_SX128X_IRQ_RX_DONE) {
    if (!receiving || (requireExternalRfSwitch && pins[9] == 0)) return false;
    irq = packetIrq & enabledIrqs;
    if (rxPeriod != RADIOLIB_SX128X_RX_TIMEOUT_INF) receiving = false;
    return true;
  }
  void deliverScheduled() {
    if (scheduledIrq != 0 && now >= scheduledAt) {
      const auto packetIrq = scheduledIrq;
      scheduledIrq = 0;
      (void)injectPacket(packetIrq);
    }
  }

  void loseConfiguration() {
    packetType = 0;
    frequency = 0;
    registers.fill(0);
    modulation.fill(0);
    packetParameters.fill(0);
    transmitParameters.fill(0);
    regulator = 0;
    const char version[] = "SX1280";
    std::memcpy(registers.data() + RADIOLIB_SX128X_REG_VERSION_STRING,
                version, sizeof(version) - 1);
    irq = 0;
    receiving = false;
    busy = false;
    clearIrqFault = false;
  }
  void pinMode(uint32_t, uint32_t) override {}
  void digitalWrite(uint32_t pin, uint32_t value) override {
    if (pin < pins.size()) {
      pins[pin] = value;
      ++pinWrites[pin];
    }
    if (pin == 2 && value == 0) {
      // No powered external PA is permitted during the radio reset pulse.
      assert(pins[9] == 0 && pins[10] == 0);
      ++resets;
      loseConfiguration();
    } else if (pin == 2 && value == 1) {
      // Model BUSY's delayed assertion, followed by a non-instant calibration.
      // An immediate first SPI command must fail instead of being idealized.
      bootBusyAt = now + 100;
      bootReadyAt = now + 8000;
    }
  }
  uint32_t digitalRead(uint32_t pin) override {
    if (pin == 5) return busy || permanentlyBusy || (now >= bootBusyAt && now < bootReadyAt);
    if (pin == 4) return (irq & dio1Mask) != 0 || spuriousDioHigh;
    return pin < pins.size() ? pins[pin] : 0;
  }
  void attachInterrupt(uint32_t, void (*)(void), uint32_t) override {}
  void detachInterrupt(uint32_t) override {}
  void delay(RadioLibTime_t ms) override { now += ms * 1000; deliverScheduled(); }
  void delayMicroseconds(RadioLibTime_t us) override { now += us; deliverScheduled(); }
  RadioLibTime_t millis() override { return now / 1000; }
  RadioLibTime_t micros() override { return now; }
  long pulseIn(uint32_t, uint32_t, RadioLibTime_t) override { return 0; }
  void yield() override { now += yieldUs; deliverScheduled(); }
  void spiBegin() override {}
  void spiBeginTransaction() override {}
  void spiEndTransaction() override {}
  void spiEnd() override {}
  void spiTransfer(uint8_t* out, size_t len, uint8_t* in) override {
    const bool unavailable = missingChip || now < bootReadyAt;
    std::memset(in, unavailable ? 0x00 : 0x20, len);
    if (unavailable) return;
    if (delayedCommand != 0 && out[0] == delayedCommand) now += commandDelayUs;
    ++commandCounts[out[0]];
    if (out[0] == RADIOLIB_SX128X_CMD_GET_IRQ_STATUS) {
      if (expectIdleOnIrqRead) assert(pins[9] == 0 && pins[10] == 0);
      if (busyOnIrqRead) busy = true;
    }
    if (out[0] == failReadCommand &&
        (failCommandOccurrence == 0 || commandCounts[out[0]] == failCommandOccurrence)) {
      // SX128x provides status during address/dummy bytes too; Module parses
      // statusPos=1 even for the two-byte ReadBuffer command.
      in[1] = RADIOLIB_SX128X_STATUS_CMD_ERROR;
      return;
    }
    if (out[0] == RADIOLIB_SX128X_CMD_CLEAR_IRQ_STATUS && clearIrqFault) {
      in[1] = RADIOLIB_SX128X_STATUS_CMD_ERROR;
      return;
    }
    switch (out[0]) {
      case RADIOLIB_SX128X_CMD_READ_REGISTER: {
        const uint16_t address = (uint16_t(out[1]) << 8) | out[2];
        std::memcpy(in + 4, registers.data() + address, len - 4);
        if (wrongVersion && address == RADIOLIB_SX128X_REG_VERSION_STRING) {
          std::memset(in + 4, 0, len - 4);
        }
        break;
      }
      case RADIOLIB_SX128X_CMD_WRITE_REGISTER: {
        const uint16_t address = (uint16_t(out[1]) << 8) | out[2];
        std::memcpy(registers.data() + address, out + 3, len - 3);
        break;
      }
      case RADIOLIB_SX128X_CMD_SET_PACKET_TYPE:
        packetType = out[1]; ++modemWrites; break;
      case RADIOLIB_SX128X_CMD_GET_PACKET_TYPE:
        in[2] = packetType; break;
      case RADIOLIB_SX128X_CMD_SET_RF_FREQUENCY:
        frequency = (uint32_t(out[1]) << 16) | (uint32_t(out[2]) << 8) | out[3];
        ++frequencyWrites; break;
      case RADIOLIB_SX128X_CMD_SET_MODULATION_PARAMS:
        std::memcpy(modulation.data(), out + 1, modulation.size()); break;
      case RADIOLIB_SX128X_CMD_SET_PACKET_PARAMS:
        std::memcpy(packetParameters.data(), out + 1, packetParameters.size()); break;
      case RADIOLIB_SX128X_CMD_SET_TX_PARAMS:
        std::memcpy(transmitParameters.data(), out + 1, transmitParameters.size()); break;
      case RADIOLIB_SX128X_CMD_SET_REGULATOR_MODE:
        regulator = out[1]; break;
      case RADIOLIB_SX128X_CMD_SET_TX:
        receiving = false;
        if (failSetTx) busy = true;
        else if (!withholdTxDone) irq = RADIOLIB_SX128X_IRQ_TX_DONE;
        clearIrqFault = failClearAfterTx;
        break;
      case RADIOLIB_SX128X_CMD_SET_RX:
        if (requireSyncMapping) {
          assert((enabledIrqs & RADIOLIB_SX128X_IRQ_SYNC_WORD_ERROR) != 0);
          assert((dio1Mask & RADIOLIB_SX128X_IRQ_SYNC_WORD_ERROR) != 0);
        }
        if (failSetRx) busy = true;
        else {
          receiving = true;
          rxPeriod = (uint16_t(out[2]) << 8) | out[3];
          ++rxStarts;
        }
        break;
      case RADIOLIB_SX128X_CMD_SET_STANDBY:
        receiving = false;
        ++standbyWrites; break;
      case RADIOLIB_SX128X_CMD_GET_IRQ_STATUS:
        in[2] = irq >> 8; in[3] = irq; break;
      case RADIOLIB_SX128X_CMD_CLEAR_IRQ_STATUS:
        irq = 0; break;
      case RADIOLIB_SX128X_CMD_GET_RX_BUFFER_STATUS:
        in[2] = rxLength; in[3] = rxOffset; break;
      case RADIOLIB_SX128X_CMD_READ_BUFFER:
        assert(out[1] == rxOffset);
        std::memcpy(in + 3, rxData.data(), len - 3); break;
      case RADIOLIB_SX128X_CMD_GET_PACKET_STATUS:
        std::memset(in + 2, 0, len - 2);
        in[3] = 148; in[4] = packetErrorStatus;
        in[6] = packetSyncStatus; break;  // FLRC RSSI = -74 dBm.
      case RADIOLIB_SX128X_CMD_SET_DIO_IRQ_PARAMS:
        enabledIrqs = (uint16_t(out[1]) << 8) | out[2];
        dio1Mask = (uint16_t(out[3]) << 8) | out[4]; break;
      default: break;
    }
  }
};

ProbeHal* adapterHal = nullptr;

void adapterDigitalWrite(int pin, int value) {
  assert(adapterHal != nullptr);
  adapterHal->digitalWrite(static_cast<uint32_t>(pin), static_cast<uint32_t>(value));
}

}  // namespace

SPIClass SPI;

// Inject the fake HAL at the adapter's Arduino boundary. The actual RadioLib
// implementations are compiled separately by run_radiolib_recovery_tests.py.
class ArduinoHal final : public ProbeHal {
 public:
  ArduinoHal(SPIClass&, SPISettings) { adapterHal = this; }
};

#define digitalWrite adapterDigitalWrite
#include "../lib/fanlink_platform_esp8266/src/fanlink/platform/esp8266/radiolib_sx1280_port.cpp"
#undef digitalWrite

// The deployed Lite profile has no external switch. Keep its explicit coverage
// separate from the legacy two-pin fault suite; absent GPIO9/10 must neither
// prevent simulated RX nor be touched by adapter/RadioLib cleanup.
void testLiteProfile() {
  using fanlink::RadioResult;
  using namespace fanlink::platform::esp8266;
  Sx1280PortConfig config;
  config.outputPowerDbm = 13;
  assert(config.rxEnablePin == -1 && config.txEnablePin == -1 && !config.useDcdc);
  RadioLibSx1280Port adapter(config);
  ProbeHal& hal = *adapterHal;
  hal.requireExternalRfSwitch = false;
  hal.requireSyncMapping = true;
  std::array<uint8_t, 30> data{};
  assert(adapter.begin(0x4b4e4c46));
  assert(hal.regulator == RADIOLIB_SX128X_REGULATOR_LDO);
  assert(hal.transmitParameters[0] == 31);  // SX128x encoding: dBm + 18.
  const auto packetParameters = hal.packetParameters;
  assert(packetParameters[3] == RADIOLIB_SX128X_GFSK_FLRC_PACKET_FIXED);
  assert(packetParameters[4] == data.size() && packetParameters[5] == 0x20);
  assert(adapter.transmit(data.data(), data.size()) == RadioResult::Ok);
  assert(adapter.receiveForUs(data.data(), data.size(), 100).result == RadioResult::Timeout);
  assert(hal.receiving);
  const auto starts = hal.rxStarts;
  hal.rxOffset = 17;
  hal.rxData.fill(0xA6);
  assert(hal.injectPacket());
  assert(adapter.receiveForUs(data.data(), data.size(), 100).result == RadioResult::Ok);
  assert(data == hal.rxData && hal.receiving && hal.rxStarts == starts + 1);
  hal.packetErrorStatus = RADIOLIB_SX128X_PACKET_STATUS_CRC_ERROR;
  assert(hal.injectPacket(RADIOLIB_SX128X_IRQ_RX_DONE | RADIOLIB_SX128X_IRQ_CRC_ERROR));
  const auto readsBeforeCrc = hal.commandCounts[RADIOLIB_SX128X_CMD_READ_BUFFER];
  assert(adapter.receiveForUs(data.data(), data.size(), 100).result == RadioResult::CrcError);
  assert(hal.commandCounts[RADIOLIB_SX128X_CMD_READ_BUFFER] == readsBeforeCrc);
  assert(hal.receiving);
  hal.packetErrorStatus = 0;
  assert(hal.injectPacket(RADIOLIB_SX128X_IRQ_SYNC_WORD_ERROR));
  assert(adapter.receiveForUs(data.data(), data.size(), 100).result == RadioResult::CrcError);
  assert(hal.receiving);
  assert(hal.injectPacket());
  assert(adapter.receiveForUs(data.data(), data.size(), 100).result == RadioResult::Ok);
  assert(adapter.transmit(data.data(), data.size()) == RadioResult::Ok);
  assert(hal.packetParameters == packetParameters);
  assert(adapter.begin(0x4b4e4c46));
  assert(hal.packetParameters == packetParameters);
  assert(hal.regulator == RADIOLIB_SX128X_REGULATOR_LDO);
  assert(hal.transmitParameters[0] == 31);
  assert(hal.pinWrites[9] == 0 && hal.pinWrites[10] == 0);
  std::puts("PASS: Lite -1/-1 LDO +13dBm TX/RX/CRC/sync/recovery; no GPIO9/10 writes");
}

// Former red regression, now part of the default suite: reject false success
// with no TX_DONE and a stuck-high DIO. This is simulated fault evidence,
// not evidence that the live return-path CRC errors have this cause.
int reproduceMissingTxDoneWithHighDio() {
  using fanlink::RadioResult;
  using namespace fanlink::platform::esp8266;
  Sx1280PortConfig config;
  config.outputPowerDbm = 13;
  RadioLibSx1280Port adapter(config);
  ProbeHal& hal = *adapterHal;
  hal.requireExternalRfSwitch = false;
  std::array<uint8_t, 30> data{};
  assert(adapter.begin(0x4b4e4c46));
  assert(adapter.transmit(data.data(), data.size()) == RadioResult::Ok);
  hal.withholdTxDone = true;
  assert(adapter.transmit(data.data(), data.size()) != RadioResult::Ok);
  assert(adapter.lastError() == RADIOLIB_ERR_TX_TIMEOUT);
  assert(hal.irq == 0);
  hal.spuriousDioHigh = true;
  const auto started = hal.now;
  const auto irqReads = hal.commandCounts[RADIOLIB_SX128X_CMD_GET_IRQ_STATUS];
  const auto txStarts = hal.commandCounts[RADIOLIB_SX128X_CMD_SET_TX];
  const auto result = adapter.transmit(data.data(), data.size());
  assert(hal.commandCounts[RADIOLIB_SX128X_CMD_SET_TX] == txStarts + 1);
  assert(hal.irq == 0 && hal.pinWrites[9] == 0 && hal.pinWrites[10] == 0);
  assert(result == RadioResult::IoError && adapter.lastError() == RADIOLIB_ERR_TX_TIMEOUT);
  assert(hal.commandCounts[RADIOLIB_SX128X_CMD_GET_IRQ_STATUS] == irqReads + 1);
  assert(hal.now - started < 27000);
  std::printf("%s: missing TX_DONE + stuck-high DIO1: returned_ok=%d driver_error=%d "
              "irq_reads=%u elapsed_us=%lu (simulated Lite profile only)\n",
              result == RadioResult::Ok ? "FAIL" : "PASS", result == RadioResult::Ok,
              adapter.lastError(), hal.commandCounts[RADIOLIB_SX128X_CMD_GET_IRQ_STATUS] - irqReads,
              static_cast<unsigned long>(hal.now - started));
  return result == RadioResult::Ok ? 1 : 0;
}

void testAdapterTrace() {
  using namespace fanlink;
  using namespace fanlink::platform::esp8266;
  RadioLibSx1280Port adapter(Sx1280PortConfig{});
  ProbeHal& hal = *adapterHal; hal.requireExternalRfSwitch = false;
  std::array<uint8_t, 30> data{}; RadioTraceRecord row;
  assert(!adapter.canEmitDeferredTrace());
  assert(adapter.begin(0x4b4e4c46));
  assert(adapter.transmit(data.data(), data.size()) == RadioResult::Ok);
  assert(!adapter.canEmitDeferredTrace());
  assert(adapter.receiveForUs(data.data(), data.size(), 0).result == RadioResult::Timeout);
  assert(adapter.canEmitDeferredTrace());
  hal.busy = true; assert(!adapter.canEmitDeferredTrace()); hal.busy = false;
  assert(hal.injectPacket(RADIOLIB_SX128X_IRQ_RX_DONE | RADIOLIB_SX128X_IRQ_CRC_ERROR));
  assert(!adapter.canEmitDeferredTrace());
  hal.packetErrorStatus = 0x12; hal.packetSyncStatus = 1;
  const auto irqReads = hal.commandCounts[RADIOLIB_SX128X_CMD_GET_IRQ_STATUS];
  assert(adapter.receiveForUs(data.data(), data.size(), 100).result == RadioResult::CrcError);
  assert(hal.commandCounts[RADIOLIB_SX128X_CMD_GET_IRQ_STATUS] == irqReads + 1);
  assert(adapter.canEmitDeferredTrace());
  bool irq = false, packet = false, clear = false, rearmed = false;
  unsigned records = 0;
  while (adapter.peekRadioTrace(row)) {
    assert(row.kind == RadioTraceKind::Crc && row.index == records++);
    const auto& s = row.sample;
    if (s.event == RadioTraceEvent::IrqRead) { irq = true; assert(s.value == 0x42 && s.validity == 15); }
    if (s.event == RadioTraceEvent::PacketStatusRead) { packet = true; assert((s.value & 0xFF00) == 0x1200 && s.validity == 15); }
    if (s.event == RadioTraceEvent::ClearEnd) clear = true;
    if (clear && s.event == RadioTraceEvent::ArmEnd) rearmed = true;
    adapter.popRadioTrace();
  }
  assert(irq && packet && clear && rearmed && records <= 32);
  hal.packetErrorStatus = 0;
  for (unsigned i = 0; i < 64; ++i) {
    assert(hal.injectPacket());
    assert(adapter.receiveForUs(data.data(), data.size(), 100).result == RadioResult::Ok);
    assert(adapter.peekRadioTrace(row) == (i == 63));
  }
  assert(row.kind == RadioTraceKind::Good);
  while (adapter.peekRadioTrace(row)) adapter.popRadioTrace();
  assert(hal.injectPacket()); hal.failReadCommand = RADIOLIB_SX128X_CMD_GET_PACKET_STATUS;
  assert(adapter.receiveForUs(data.data(), data.size(), 100).result == RadioResult::IoError);
  bool invalid = false, cleanup = false;
  while (adapter.peekRadioTrace(row)) {
    assert(row.kind == RadioTraceKind::Io);
    if (row.sample.event == RadioTraceEvent::PacketStatusRead && row.sample.result != 0) {
      assert((row.sample.validity & radio_trace_valid::kValue) == 0); invalid = true;
    }
    if (row.sample.event == RadioTraceEvent::FaultCleanup) cleanup = true;
    adapter.popRadioTrace();
  }
  assert(invalid && cleanup && !adapter.canEmitDeferredTrace());
  std::puts("PASS: actual adapter trace CRC/cleanup/rearm, sampled good, failed-SPI validity and no extra IRQ reads");
}

int main(int argc, char** argv) {
  if (argc == 2 && std::strcmp(argv[1], "--reproduce-tx-dio-high") == 0) {
    return reproduceMissingTxDoneWithHighDio();
  }
  assert(argc == 1);
  testLiteProfile();
  testAdapterTrace();
  assert(reproduceMissingTxDoneWithHighDio() == 0);
  using fanlink::RadioResult;
  using fanlink::platform::esp8266::RadioLibSx1280Port;
  using fanlink::platform::esp8266::Sx1280PortConfig;
  Sx1280PortConfig config;
  config.rxEnablePin = 9;
  config.txEnablePin = 10;
  RadioLibSx1280Port adapter(config);
  ProbeHal& hal = *adapterHal;
  hal.requireSyncMapping = true;
  std::array<uint8_t, 30> data{};
  constexpr uint32_t networkId = 0x4b4e4c46;
  assert(adapter.begin(networkId));
  assert(hal.packetType == RADIOLIB_SX128X_PACKET_TYPE_FLRC);
  const auto initialRegisters = hal.registers;
  const auto initialFrequency = hal.frequency;
  const auto initialModulation = hal.modulation;
  const auto initialPacketParameters = hal.packetParameters;
  const auto initialTransmitParameters = hal.transmitParameters;
  const auto initialRegulator = hal.regulator;
  assert(initialFrequency != 0);
  assert(initialPacketParameters[3] == RADIOLIB_SX128X_GFSK_FLRC_PACKET_FIXED);
  assert(initialPacketParameters[4] == data.size());
  assert(adapter.transmit(data.data(), data.size()) == RadioResult::Ok);

  // Rebegin after configuration loss must reapply the complete radio setup.
  hal.loseConfiguration();
  hal.pins[10] = 1;  // An old PA state must be cleared before the reset pulse.
  const int resetsBefore = hal.resets;
  assert(adapter.begin(networkId));
  assert(hal.resets == resetsBefore + 1);
  assert(hal.packetType == RADIOLIB_SX128X_PACKET_TYPE_FLRC);
  assert(hal.frequency == initialFrequency && hal.registers == initialRegisters);
  assert(hal.modulation == initialModulation && hal.packetParameters == initialPacketParameters);
  assert(hal.transmitParameters == initialTransmitParameters && hal.regulator == initialRegulator);
  assert(hal.pins[9] == 0 && hal.pins[10] == 0);
  assert(adapter.transmit(data.data(), data.size()) == RadioResult::Ok);
  assert(adapter.begin(networkId));  // Also safe without an intervening fault.
  std::puts("PASS: repeated adapter.begin restores the complete radio configuration");

  // Every failure is one discovery attempt, without a 10 x 3 s retry loop.
  for (int fault = 0; fault < 3; ++fault) {
    hal.permanentlyBusy = fault == 0;
    hal.missingChip = fault == 1;
    hal.wrongVersion = fault == 2;
    const auto start = hal.now;
    const int priorResets = hal.resets;
    assert(!adapter.begin(networkId));
    const auto elapsed = hal.now - start;
    assert(hal.resets == priorResets + 1);
    assert(elapsed < 40000);
    assert(hal.pins[9] == 0 && hal.pins[10] == 0);
    assert(adapter.transmit(data.data(), data.size()) == RadioResult::IoError);
    std::printf("PASS: begin fault=%s elapsed_us=%lu reset_attempts=1\n",
                fault == 0 ? "busy_stuck" : fault == 1 ? "missing_chip" : "wrong_version",
                static_cast<unsigned long>(elapsed));
  }
  hal.permanentlyBusy = hal.missingChip = hal.wrongVersion = false;
  assert(adapter.begin(networkId));
  assert(adapter.transmit(data.data(), data.size()) == RadioResult::Ok);

  // A bounded software wait must not turn the radio off between loop calls.
  // Inject the next downlink exactly while controller/LED/yield work runs.
  const auto waitStarted = hal.now;
  hal.delayedCommand = RADIOLIB_SX128X_CMD_SET_RX;
  hal.commandDelayUs = 200;
  const auto initialWait = adapter.receive(data.data(), data.size(), 2);
  hal.delayedCommand = 0;
  assert(initialWait.result == RadioResult::Timeout);
  assert(initialWait.evidence.receiveStarted);
  assert(uint32_t(initialWait.evidence.receiveStartedUs - waitStarted) >= 200);
  assert(uint32_t(initialWait.evidence.receiveStartedUs - waitStarted) < 220);
  assert(!initialWait.evidence.eventObserved && !initialWait.evidence.irqValid);
  assert(!initialWait.evidence.packetStatusValid && initialWait.evidence.errorFlags == 0);
  assert(initialWait.evidence.eventObservedUs == 0 && initialWait.evidence.irq == 0);
  assert(hal.now - waitStarted <= 2200);
  assert(hal.receiving && hal.pins[9] == 1 && hal.pins[10] == 0);
  assert(hal.rxPeriod == RADIOLIB_SX128X_RX_TIMEOUT_NONE);
  // Single FLRC reception may stop on sync failure without RxDone. The
  // adapter must observe that event, not retain a fictional armed state.
  assert(hal.injectPacket(RADIOLIB_SX128X_IRQ_SYNC_WORD_ERROR));
  const auto statusReadsBeforeSync = hal.commandCounts[RADIOLIB_SX128X_CMD_GET_PACKET_STATUS];
  const auto payloadReadsBeforeSync = hal.commandCounts[RADIOLIB_SX128X_CMD_READ_BUFFER];
  const auto syncObserved = uint32_t(hal.now);
  hal.delayedCommand = RADIOLIB_SX128X_CMD_SET_RX;
  hal.commandDelayUs = 300;
  const auto syncFailure = adapter.receive(data.data(), data.size(), 2);
  const auto syncRearmedAt = uint32_t(hal.now);
  hal.delayedCommand = 0;
  assert(syncFailure.result == RadioResult::CrcError);
  assert(!syncFailure.receiveObserved && syncFailure.receiveObservedUs == 0);
  assert(syncFailure.evidence.eventObserved && syncFailure.evidence.eventObservedUs == syncObserved);
  assert(syncFailure.evidence.receiveStarted && syncFailure.evidence.receiveStartedUs ==
      initialWait.evidence.receiveStartedUs);
  assert(syncFailure.evidence.irqValid && syncFailure.evidence.irq ==
      RADIOLIB_SX128X_IRQ_SYNC_WORD_ERROR);
  assert(syncFailure.evidence.errorFlags == (fanlink::radio_rx_error::kSyncWithoutRxDone |
      fanlink::radio_rx_error::kIrqSync));
  assert(!syncFailure.evidence.packetStatusValid && syncFailure.evidence.errorRssiDbm == -127);
  assert(hal.commandCounts[RADIOLIB_SX128X_CMD_GET_PACKET_STATUS] == statusReadsBeforeSync);
  assert(hal.commandCounts[RADIOLIB_SX128X_CMD_READ_BUFFER] == payloadReadsBeforeSync);
  const auto afterSync = adapter.receiveForUs(data.data(), data.size(), 0);
  assert(afterSync.result == RadioResult::Timeout && afterSync.evidence.receiveStarted);
  assert(afterSync.evidence.receiveStartedUs == syncRearmedAt);
  assert(uint32_t(syncRearmedAt - syncObserved) >= 300);
  assert(!afterSync.evidence.eventObserved && !afterSync.evidence.irqValid);
  assert(!afterSync.evidence.packetStatusValid && afterSync.evidence.errorFlags == 0);
  assert(hal.receiving && hal.irq == 0);
  std::puts("PASS: sync-only stopped single RX is observed and rearmed");
  const auto startsAfterWait = hal.rxStarts;
  hal.rxData.fill(0xA5);
  hal.rxOffset = 7;
  hal.packetSyncStatus = RADIOLIB_SX128X_PACKET_STATUS_SYNC_DET_1;
  assert(hal.injectPacket());
  const auto pending = adapter.receive(data.data(), data.size(), 2);
  assert(pending.result == RadioResult::Ok && pending.length == data.size());
  assert(data == hal.rxData && pending.rssiDbm == -74);
  assert(pending.receiveObserved);
  assert(pending.evidence.irqValid && pending.evidence.packetStatusValid);
  assert(pending.evidence.irq == RADIOLIB_SX128X_IRQ_RX_DONE);
  assert(pending.evidence.packetStatusError == 0 && pending.evidence.errorFlags == 0);
  assert(pending.evidence.packetStatusSync == RADIOLIB_SX128X_PACKET_STATUS_SYNC_DET_1);
  assert(pending.evidence.receiveStartedUs == afterSync.evidence.receiveStartedUs);
  assert(pending.evidence.eventObservedUs == pending.receiveObservedUs);
  assert(pending.evidence.errorRssiDbm == -127);
  assert(hal.rxStarts == startsAfterWait + 1);  // Rearm AFTER consuming it.
  assert(hal.receiving && hal.pins[9] == 1 && hal.pins[10] == 0);
  std::puts("PASS: packet arriving between software waits is retained; rearm follows read");

  // Real driver processing can finish after the controller deadline. Only the
  // DIO observation is timestamped, never completion or a fictitious IRQ edge.
  hal.yieldUs = 1;
  for (const uint32_t start : {uint32_t(1000000), uint32_t(0xFFFFFF00)}) {
    hal.now = start;
    hal.scheduledAt = hal.now + 700;
    hal.scheduledIrq = RADIOLIB_SX128X_IRQ_RX_DONE;
    hal.delayedCommand = RADIOLIB_SX128X_CMD_READ_BUFFER;
    hal.commandDelayUs = 6000;
    const auto timed = adapter.receiveForUs(data.data(), data.size(), 750);
    assert(timed.result == RadioResult::Ok && timed.receiveObserved);
    assert(timed.evidence.eventObserved && timed.evidence.eventObservedUs == timed.receiveObservedUs);
    assert(uint32_t(timed.receiveObservedUs - start) == 700);
    assert(uint32_t(hal.now - start) >= 6700);
    const auto rearmedAt = uint32_t(hal.now);
    hal.delayedCommand = 0;
    const auto noPacket = adapter.receiveForUs(data.data(), data.size(), 750);
    assert(noPacket.result == RadioResult::Timeout);
    assert(!noPacket.receiveObserved && noPacket.receiveObservedUs == 0);
    assert(noPacket.evidence.receiveStarted && !noPacket.evidence.eventObserved);
    assert(noPacket.evidence.receiveStartedUs == rearmedAt);
    assert(!noPacket.evidence.irqValid && !noPacket.evidence.packetStatusValid);
    assert(noPacket.evidence.irq == 0 && noPacket.evidence.packetStatusError == 0);
  }
  hal.now = 2000000;
  for (const uint32_t at : {4999U, 5000U}) {
    const auto start = uint32_t(hal.now);
    hal.scheduledAt = hal.now + at;
    hal.scheduledIrq = RADIOLIB_SX128X_IRQ_RX_DONE;
    const auto boundary = adapter.receiveForUs(data.data(), data.size(), 5000);
    assert(boundary.result == RadioResult::Ok && boundary.receiveObserved);
    assert(uint32_t(boundary.receiveObservedUs - start) == at);
    // Controller owns the strict < deadline policy (5000 is late), not HAL.
  }
  const auto zeroStart = hal.now;
  assert(adapter.receiveForUs(data.data(), data.size(), 0).result == RadioResult::Timeout);
  assert(hal.now == zeroStart);
  assert(hal.injectPacket());
  const auto zeroPending = adapter.receiveForUs(data.data(), data.size(), 0);
  assert(zeroPending.result == RadioResult::Ok && zeroPending.receiveObserved);
  assert(zeroPending.receiveObservedUs == uint32_t(zeroStart));
  hal.yieldUs = 100;
  std::puts("PASS: fractional/zero budgets and 4999/5000 us observation; delayed processing and micros wrap");

  // Repeated timeouts do not issue SetRx again and clear an in-progress packet.
  const auto startsBeforeTimeouts = hal.rxStarts;
  for (unsigned wait = 0; wait < 20; ++wait) {
    assert(adapter.receive(data.data(), data.size(), 1).result == RadioResult::Timeout);
  }
  assert(hal.rxStarts == startsBeforeTimeouts && hal.receiving);
  hal.scheduledAt = hal.now + 500;
  hal.scheduledIrq = RADIOLIB_SX128X_IRQ_RX_DONE;
  assert(adapter.receive(data.data(), data.size(), 1).result == RadioResult::Ok);
  assert(hal.receiving);
  std::puts("PASS: repeated waits preserve RX and a packet completes during a later wait");

  // CRC does not refresh a controller, but must promptly rearm for the next frame.
  assert(hal.injectPacket(RADIOLIB_SX128X_IRQ_RX_DONE | RADIOLIB_SX128X_IRQ_CRC_ERROR));
  const auto crcPacket = adapter.receive(data.data(), data.size(), 1);
  assert(crcPacket.result == RadioResult::CrcError && crcPacket.receiveObserved);
  assert(crcPacket.rssiDbm == -127 && crcPacket.evidence.errorRssiDbm == -74);
  assert(crcPacket.evidence.errorFlags == fanlink::radio_rx_error::kIrqCrc);
  assert(hal.receiving && hal.irq == 0 && hal.pins[10] == 0);
  assert(hal.injectPacket());
  assert(adapter.receive(data.data(), data.size(), 1).result == RadioResult::Ok);
  std::puts("PASS: CRC packet rearms single RX and the following valid packet is received");

  // Causes are separate, may overlap, and must not alter CRC acceptance. A
  // HeaderError accompanied by HeaderValid retains the deployed accept rule.
  struct ErrorCase { uint16_t irq; uint8_t packet; uint8_t expected; };
  for (const auto& error : {
      ErrorCase{RADIOLIB_SX128X_IRQ_CRC_ERROR, 0, fanlink::radio_rx_error::kIrqCrc},
      ErrorCase{0, RADIOLIB_SX128X_PACKET_STATUS_CRC_ERROR, fanlink::radio_rx_error::kPacketCrc},
      ErrorCase{RADIOLIB_SX128X_IRQ_SYNC_WORD_ERROR, 0, fanlink::radio_rx_error::kIrqSync},
      ErrorCase{RADIOLIB_SX128X_IRQ_HEADER_ERROR, 0, fanlink::radio_rx_error::kHeader},
      ErrorCase{RADIOLIB_SX128X_IRQ_HEADER_ERROR | RADIOLIB_SX128X_IRQ_HEADER_VALID, 0, 0},
      ErrorCase{RADIOLIB_SX128X_IRQ_CRC_ERROR | RADIOLIB_SX128X_IRQ_HEADER_ERROR |
          RADIOLIB_SX128X_IRQ_SYNC_WORD_ERROR, RADIOLIB_SX128X_PACKET_STATUS_CRC_ERROR,
          fanlink::radio_rx_error::kIrqCrc | fanlink::radio_rx_error::kPacketCrc |
          fanlink::radio_rx_error::kHeader | fanlink::radio_rx_error::kIrqSync}}) {
    const auto receiveStart = adapter.receiveForUs(data.data(), data.size(), 0).evidence.receiveStartedUs;
    hal.packetErrorStatus = error.packet;
    assert(hal.injectPacket(RADIOLIB_SX128X_IRQ_RX_DONE | error.irq));
    const auto readCounts = hal.commandCounts;
    const auto eventTime = uint32_t(hal.now);
    hal.delayedCommand = RADIOLIB_SX128X_CMD_SET_RX;
    hal.commandDelayUs = 100;
    data.fill(0x5a);
    const auto classified = adapter.receiveForUs(data.data(), data.size(), 1000);
    const auto rearmedAt = uint32_t(hal.now);
    hal.delayedCommand = 0;
    assert(classified.result == (error.expected == 0 ? RadioResult::Ok : RadioResult::CrcError));
    assert(classified.evidence.irqValid && classified.evidence.packetStatusValid);
    assert(classified.evidence.errorFlags == error.expected);
    assert(classified.evidence.irq == (RADIOLIB_SX128X_IRQ_RX_DONE | error.irq));
    assert(classified.evidence.packetStatusError == error.packet);
    assert(classified.evidence.packetStatusSync == hal.packetSyncStatus);
    assert(classified.evidence.receiveStarted && classified.evidence.receiveStartedUs == receiveStart);
    assert(classified.evidence.eventObservedUs == eventTime);
    assert(classified.evidence.errorRssiDbm == (error.expected == 0 ? -127 : -74));
    assert(classified.rssiDbm == (error.expected == 0 ? -74 : -127));
    assert(hal.commandCounts[RADIOLIB_SX128X_CMD_GET_IRQ_STATUS] ==
        readCounts[RADIOLIB_SX128X_CMD_GET_IRQ_STATUS] + 1);
    assert(hal.commandCounts[RADIOLIB_SX128X_CMD_GET_RX_BUFFER_STATUS] ==
        readCounts[RADIOLIB_SX128X_CMD_GET_RX_BUFFER_STATUS] + 1);
    assert(hal.commandCounts[RADIOLIB_SX128X_CMD_GET_PACKET_STATUS] ==
        readCounts[RADIOLIB_SX128X_CMD_GET_PACKET_STATUS] + 1);
    assert(hal.commandCounts[RADIOLIB_SX128X_CMD_READ_BUFFER] ==
        readCounts[RADIOLIB_SX128X_CMD_READ_BUFFER] + (error.expected == 0 ? 1U : 0U));
    if (error.expected != 0) for (const auto byte : data) assert(byte == 0x5a);
    const auto nextWait = adapter.receiveForUs(data.data(), data.size(), 0);
    assert(nextWait.evidence.receiveStartedUs == rearmedAt);
    assert(uint32_t(rearmedAt - eventTime) >= 100);
    assert(!nextWait.evidence.eventObserved && !nextWait.evidence.irqValid);
    assert(nextWait.evidence.errorFlags == 0 && nextWait.evidence.errorRssiDbm == -127);
    hal.packetErrorStatus = 0;
    assert(hal.injectPacket());
    const auto following = adapter.receiveForUs(data.data(), data.size(), 1000);
    assert(following.result == RadioResult::Ok && following.evidence.errorFlags == 0);
    assert(following.evidence.errorRssiDbm == -127 && data == hal.rxData);
  }
  std::puts("PASS: separate/overlapping CRC causes preserve raw metadata and reject without payload reads");

  // Starting a TX discards pending old RX IRQ/FIFO state; it cannot be replayed
  // as the response to the next poll, and the external PA is released afterward.
  assert(hal.injectPacket());
  assert(adapter.transmit(data.data(), data.size()) == RadioResult::Ok);
  assert(hal.irq == 0 && !hal.receiving && hal.pins[9] == 0 && hal.pins[10] == 0);
  assert(adapter.receive(data.data(), data.size(), 1).result == RadioResult::Timeout);
  assert(hal.injectPacket());
  assert(adapter.begin(networkId));
  assert(hal.irq == 0 && !hal.receiving);
  assert(adapter.receive(data.data(), data.size(), 1).result == RadioResult::Timeout);
  std::puts("PASS: transmit and radio recovery invalidate any pending old receive state");

  hal.now = 0xFFFFFF00UL;
  const auto wrapStart = hal.now;
  assert(adapter.receive(data.data(), data.size(), 1).result == RadioResult::Timeout);
  assert(hal.now - wrapStart >= 1000 && hal.now - wrapStart <= 1200);
  assert(hal.receiving);
  assert(adapter.receive(nullptr, data.size(), 1).result == RadioResult::IoError);
  assert(hal.pins[9] == 0 && hal.pins[10] == 0);
  assert(adapter.begin(networkId));
  std::puts("PASS: software receive timeout spans micros wrap; argument errors release RF pins");

  // A correctly read packet cannot be reported as valid if rearming then fails.
  assert(adapter.receive(data.data(), data.size(), 1).result == RadioResult::Timeout);
  assert(hal.injectPacket());
  hal.failSetRx = true;
  const auto rearmFaultStart = hal.now;
  const auto rearmFailure = adapter.receive(data.data(), data.size(), 1);
  assert(rearmFailure.result == RadioResult::IoError);
  assert(!rearmFailure.receiveObserved && rearmFailure.rssiDbm == -127);
  assert(rearmFailure.evidence.receiveStarted && rearmFailure.evidence.eventObserved);
  assert(rearmFailure.evidence.eventObservedUs == uint32_t(rearmFaultStart));
  assert(rearmFailure.evidence.irqValid && rearmFailure.evidence.packetStatusValid);
  assert(rearmFailure.evidence.irq == RADIOLIB_SX128X_IRQ_RX_DONE);
  assert(rearmFailure.evidence.errorRssiDbm == -74);
  assert(hal.now - rearmFaultStart < 27000);
  assert(hal.pins[9] == 0 && hal.pins[10] == 0);
  hal.failSetRx = hal.busy = false;
  assert(adapter.begin(networkId));
  std::puts("PASS: rearm failure is bounded, discards success and releases RF front end");

  for (const uint8_t badLength : {uint8_t(29), uint8_t(31)}) {
    assert(adapter.receive(data.data(), data.size(), 1).result == RadioResult::Timeout);
    hal.rxLength = badLength;
    assert(hal.injectPacket());
    assert(adapter.receive(data.data(), data.size(), 1).result == RadioResult::IoError);
    assert(hal.pins[9] == 0 && hal.pins[10] == 0);
    hal.rxLength = 30;
    assert(adapter.begin(networkId));
  }
  assert(adapter.receive(data.data(), data.size(), 1).result == RadioResult::Timeout);
  assert(hal.injectPacket(RADIOLIB_SX128X_IRQ_TX_DONE));
  hal.spuriousDioHigh = true;  // Electrical/stuck DIO, not a mapped RX interrupt.
  assert(adapter.receive(data.data(), data.size(), 1).result == RadioResult::IoError);
  hal.spuriousDioHigh = false;
  assert(hal.pins[9] == 0 && hal.pins[10] == 0);
  assert(adapter.begin(networkId));
  std::puts("PASS: wrong IRQ and short/long RF payloads cannot masquerade as a 30-byte packet");

  for (const uint8_t failedCommand : {RADIOLIB_SX128X_CMD_SET_STANDBY,
                                     RADIOLIB_SX128X_CMD_GET_IRQ_STATUS,
                                     RADIOLIB_SX128X_CMD_GET_RX_BUFFER_STATUS,
                                     RADIOLIB_SX128X_CMD_GET_PACKET_STATUS,
                                     RADIOLIB_SX128X_CMD_READ_BUFFER,
                                     RADIOLIB_SX128X_CMD_CLEAR_IRQ_STATUS}) {
    assert(adapter.receive(data.data(), data.size(), 1).result == RadioResult::Timeout);
    assert(hal.injectPacket());
    const auto eventTime = uint32_t(hal.now);
    hal.failReadCommand = failedCommand;
    const auto failure = adapter.receive(data.data(), data.size(), 1);
    if (failure.result != RadioResult::IoError) {
      std::fprintf(stderr, "unexpected receive success for failed opcode 0x%02x\n", failedCommand);
    }
    assert(failure.result == RadioResult::IoError && failure.length == 0);
    assert(!failure.receiveObserved && failure.receiveObservedUs == 0);
    assert(failure.evidence.receiveStarted && failure.evidence.eventObserved);
    assert(failure.evidence.eventObservedUs == eventTime);
    const bool irqValid = failedCommand != RADIOLIB_SX128X_CMD_SET_STANDBY &&
        failedCommand != RADIOLIB_SX128X_CMD_GET_IRQ_STATUS;
    const bool statusValid = failedCommand == RADIOLIB_SX128X_CMD_READ_BUFFER ||
        failedCommand == RADIOLIB_SX128X_CMD_CLEAR_IRQ_STATUS;
    assert(failure.evidence.irqValid == irqValid);
    assert(failure.evidence.irq == (irqValid ? RADIOLIB_SX128X_IRQ_RX_DONE : 0));
    assert(failure.evidence.packetStatusValid == statusValid);
    assert(failure.evidence.packetStatusSync == (statusValid ? hal.packetSyncStatus : 0));
    assert(failure.evidence.errorRssiDbm == (statusValid ? -74 : -127));
    assert(failure.rssiDbm == -127);
    assert(failure.driverError == RADIOLIB_ERR_SPI_CMD_INVALID);
    assert(hal.pins[9] == 0 && hal.pins[10] == 0);
    hal.failReadCommand = 0;
    assert(adapter.begin(networkId));
  }
  // Cleanup failures cannot erase the radio cause already read for THIS
  // rejected event, nor attach stale IRQ/status to the next software wait.
  for (const bool syncOnly : {false, true}) {
    for (const bool failRearm : {false, true}) {
      const auto armed = adapter.receiveForUs(data.data(), data.size(), 0);
      assert(armed.result == RadioResult::Timeout);
      const uint16_t packetIrq = syncOnly ? RADIOLIB_SX128X_IRQ_SYNC_WORD_ERROR :
          RADIOLIB_SX128X_IRQ_RX_DONE | RADIOLIB_SX128X_IRQ_CRC_ERROR;
      assert(hal.injectPacket(packetIrq));
      const auto eventTime = uint32_t(hal.now);
      const auto previousReads = hal.commandCounts;
      hal.failSetRx = failRearm;
      hal.failReadCommand = failRearm ? 0 : RADIOLIB_SX128X_CMD_CLEAR_IRQ_STATUS;
      const auto failure = adapter.receiveForUs(data.data(), data.size(), 1000);
      assert(failure.result == RadioResult::IoError && failure.length == 0);
      assert(!failure.receiveObserved && failure.rssiDbm == -127);
      assert(failure.evidence.eventObserved && failure.evidence.eventObservedUs == eventTime);
      assert(failure.evidence.receiveStarted && failure.evidence.receiveStartedUs ==
          armed.evidence.receiveStartedUs);
      assert(failure.evidence.irqValid && failure.evidence.irq == packetIrq);
      assert(failure.evidence.packetStatusValid == !syncOnly);
      assert(failure.evidence.errorFlags == (syncOnly ?
          fanlink::radio_rx_error::kSyncWithoutRxDone | fanlink::radio_rx_error::kIrqSync :
          fanlink::radio_rx_error::kIrqCrc));
      assert(failure.evidence.errorRssiDbm == (syncOnly ? -127 : -74));
      assert(hal.commandCounts[RADIOLIB_SX128X_CMD_READ_BUFFER] ==
          previousReads[RADIOLIB_SX128X_CMD_READ_BUFFER]);
      assert(hal.commandCounts[RADIOLIB_SX128X_CMD_GET_PACKET_STATUS] ==
          previousReads[RADIOLIB_SX128X_CMD_GET_PACKET_STATUS] + (syncOnly ? 0U : 1U));
      hal.failSetRx = hal.busy = false;
      hal.failReadCommand = 0;
      const auto recoveredWait = adapter.receiveForUs(data.data(), data.size(), 0);
      assert(recoveredWait.result == RadioResult::Timeout);
      assert(recoveredWait.evidence.receiveStarted && !recoveredWait.evidence.eventObserved);
      assert(!recoveredWait.evidence.irqValid && !recoveredWait.evidence.packetStatusValid);
      assert(recoveredWait.evidence.errorFlags == 0 && recoveredWait.evidence.errorRssiDbm == -127);
      assert(recoveredWait.evidence.receiveStartedUs != failure.evidence.receiveStartedUs);
      assert(hal.injectPacket());
      assert(adapter.receiveForUs(data.data(), data.size(), 1000).result == RadioResult::Ok);
    }
  }
  std::puts("PASS: CRC/sync metadata survives cleanup/rearm failure; later waits have no stale event");
  assert(adapter.receive(data.data(), data.size(), 0).result == RadioResult::Timeout);
  hal.busy = true;  // No IRQ: software wait still returns to the RX watchdog.
  const auto silentBusyStart = hal.now;
  assert(adapter.receive(data.data(), data.size(), 1).result == RadioResult::Timeout);
  assert(hal.now - silentBusyStart == 1000);
  hal.busy = false;
  assert(adapter.receive(data.data(), data.size(), UINT32_MAX).result == RadioResult::IoError);
  assert(hal.pins[9] == 0 && hal.pins[10] == 0);
  assert(adapter.begin(networkId));
  std::puts("PASS: metadata SPI errors fail closed; no-IRQ BUSY and zero/overflow waits are bounded");

  assert(adapter.receive(data.data(), data.size(), 1).result == RadioResult::Timeout);
  hal.packetErrorStatus = RADIOLIB_SX128X_PACKET_STATUS_CRC_ERROR;
  assert(hal.injectPacket());  // RxDone alone does not establish CRC validity.
  assert(adapter.receive(data.data(), data.size(), 1).result == RadioResult::CrcError);
  assert(hal.receiving && hal.irq == 0);
  hal.packetErrorStatus = 0;
  for (const uint8_t packetFault : {RADIOLIB_SX128X_PACKET_STATUS_SYNC_ERROR,
                                    RADIOLIB_SX128X_PACKET_STATUS_LENGTH_ERROR,
                                    RADIOLIB_SX128X_PACKET_STATUS_ABORT_ERROR}) {
    hal.packetErrorStatus = packetFault;
    assert(hal.injectPacket());
    const auto priorPayloadReads = hal.commandCounts[RADIOLIB_SX128X_CMD_READ_BUFFER];
    const auto packetFailure = adapter.receive(data.data(), data.size(), 1);
    assert(packetFailure.result == RadioResult::IoError);
    assert(packetFailure.evidence.irqValid && packetFailure.evidence.packetStatusValid);
    assert(packetFailure.evidence.packetStatusError == packetFault);
    assert(packetFailure.evidence.errorRssiDbm == -74 && packetFailure.rssiDbm == -127);
    assert(hal.commandCounts[RADIOLIB_SX128X_CMD_READ_BUFFER] == priorPayloadReads);
    assert(hal.pins[9] == 0 && hal.pins[10] == 0);
    hal.packetErrorStatus = 0;
    assert(adapter.begin(networkId));
    assert(adapter.receive(data.data(), data.size(), 1).result == RadioResult::Timeout);
  }
  for (const uint8_t metadataCommand : {RADIOLIB_SX128X_CMD_GET_IRQ_STATUS,
                                       RADIOLIB_SX128X_CMD_GET_RX_BUFFER_STATUS}) {
    assert(hal.injectPacket());
    const auto previousReads = hal.commandCounts[metadataCommand];
    hal.failReadCommand = metadataCommand;
    hal.failCommandOccurrence = previousReads + 2;
    assert(adapter.receive(data.data(), data.size(), 1).result == RadioResult::Ok);
    assert(hal.commandCounts[metadataCommand] == previousReads + 1);
    assert(data == hal.rxData);  // No unchecked re-read can reuse an old buffer.
    hal.failReadCommand = 0;
    hal.failCommandOccurrence = 0;
  }
  std::puts("PASS: packet-status CRC is rejected; checked IRQ/length are consumed without rereads");

  // The second IRQ mapping command adds FLRC sync routing before SetRx.
  // A failure there must not launch RX with the incomplete generic mapping.
  assert(adapter.transmit(data.data(), data.size()) == RadioResult::Ok);
  hal.failReadCommand = RADIOLIB_SX128X_CMD_SET_DIO_IRQ_PARAMS;
  hal.failCommandOccurrence = hal.commandCounts[hal.failReadCommand] + 2;
  const auto startsBeforeMappingFault = hal.rxStarts;
  const auto mappingFailure = adapter.receiveForUs(data.data(), data.size(), 750);
  assert(mappingFailure.result == RadioResult::IoError);
  assert(!mappingFailure.evidence.receiveStarted && !mappingFailure.evidence.eventObserved);
  assert(!mappingFailure.evidence.irqValid && !mappingFailure.evidence.packetStatusValid);
  assert(hal.rxStarts == startsBeforeMappingFault && !hal.receiving);
  assert(hal.pins[9] == 0 && hal.pins[10] == 0);
  hal.failReadCommand = 0;
  hal.failCommandOccurrence = 0;
  assert(adapter.receiveForUs(data.data(), data.size(), 750).result == RadioResult::Timeout);
  assert(hal.receiving);
  std::puts("PASS: sync IRQ mapping failure does not launch RX; later receive safely retries");

  // Exercise the patched driver directly, so wrapper cleanup cannot hide a
  // regression in launchMode or finishTransmit.
  ProbeHal driverHal;
  Module module(&driverHal, 15, 4, 2, 5);
  SX1280 radio(&module);
  module.spiConfig.timeout = 25;
  radio.setRfSwitchPins(9, 10);
  assert(radio.beginFLRC(2405.0F, 650, 3, -18, 16, RADIOLIB_SHAPING_0_5) == 0);
  assert(radio.fixedPacketLengthMode(30) == 0);
  driverHal.withholdTxDone = true;
  const auto noIrqStart = driverHal.now;
  assert(radio.transmit(data.data(), data.size()) == RADIOLIB_ERR_TX_TIMEOUT);
  const auto noIrqElapsed = driverHal.now - noIrqStart;
  assert(noIrqElapsed < 27000 && driverHal.pins[10] == 0 && driverHal.irq == 0);
  std::printf("PASS: withheld TX_DONE elapsed_us=%lu PA=idle IRQ=clear\n",
              static_cast<unsigned long>(noIrqElapsed));
  driverHal.withholdTxDone = false;

  driverHal.expectIdleOnIrqRead = true;
  const auto normalIrqReads = driverHal.commandCounts[RADIOLIB_SX128X_CMD_GET_IRQ_STATUS];
  assert(radio.transmit(data.data(), data.size()) == RADIOLIB_ERR_NONE);
  assert(driverHal.commandCounts[RADIOLIB_SX128X_CMD_GET_IRQ_STATUS] == normalIrqReads + 1);
  for (const bool busyFault : {false, true}) {
    driverHal.busyOnIrqRead = busyFault;
    driverHal.failReadCommand = busyFault ? 0 : RADIOLIB_SX128X_CMD_GET_IRQ_STATUS;
    const auto faultStart = driverHal.now;
    const auto clears = driverHal.commandCounts[RADIOLIB_SX128X_CMD_CLEAR_IRQ_STATUS];
    const auto standbys = driverHal.standbyWrites;
    const int expected = busyFault ? RADIOLIB_ERR_SPI_CMD_TIMEOUT : RADIOLIB_ERR_SPI_CMD_INVALID;
    assert(radio.transmit(data.data(), data.size()) == expected);
    assert(driverHal.now - faultStart < 27000);
    assert(driverHal.pins[9] == 0 && driverHal.pins[10] == 0);
    // Only preparation cleared IRQ / entered standby. Failed verification adds
    // no cleanup SPI that could hide the error or append another BUSY deadline.
    assert(driverHal.commandCounts[RADIOLIB_SX128X_CMD_CLEAR_IRQ_STATUS] == clears + 1);
    assert(driverHal.standbyWrites == standbys + 1);
    assert(radio.launchMode() == RADIOLIB_ERR_UNSUPPORTED);
    driverHal.busy = driverHal.busyOnIrqRead = false;
    driverHal.failReadCommand = 0;
    assert(radio.transmit(data.data(), data.size()) == RADIOLIB_ERR_NONE);
  }
  driverHal.withholdTxDone = driverHal.spuriousDioHigh = true;
  driverHal.failClearAfterTx = true;
  const auto missingIrqStart = driverHal.now;
  assert(radio.transmit(data.data(), data.size()) == RADIOLIB_ERR_TX_TIMEOUT);
  assert(driverHal.now - missingIrqStart < 27000);
  assert(driverHal.pins[9] == 0 && driverHal.pins[10] == 0);
  driverHal.withholdTxDone = driverHal.spuriousDioHigh = false;
  driverHal.failClearAfterTx = driverHal.clearIrqFault = false;
  driverHal.expectIdleOnIrqRead = false;
  assert(radio.transmit(data.data(), data.size()) == RADIOLIB_ERR_NONE);
  std::puts("PASS: checked TX_DONE, IRQ SPI failure/BUSY bound, idle front end and original-error preservation");

  driverHal.failSetTx = true;
  const auto txFaultStart = driverHal.now;
  assert(radio.transmit(data.data(), data.size()) == RADIOLIB_ERR_SPI_CMD_TIMEOUT);
  assert(driverHal.now - txFaultStart < 27000);
  assert(driverHal.pins[9] == 0 && driverHal.pins[10] == 0);
  assert(radio.launchMode() == RADIOLIB_ERR_UNSUPPORTED);
  driverHal.busy = driverHal.failSetTx = false;
  assert(radio.transmit(data.data(), data.size()) == 0);
  std::puts("PASS: SetTx BUSY fault releases PA and staged mode without extra SPI waits");

  driverHal.failSetRx = true;
  const auto rxFaultStart = driverHal.now;
  assert(radio.receive(data.data(), data.size(), 5000) == RADIOLIB_ERR_SPI_CMD_TIMEOUT);
  assert(driverHal.now - rxFaultStart < 27000);
  assert(driverHal.pins[9] == 0 && driverHal.pins[10] == 0);
  assert(radio.launchMode() == RADIOLIB_ERR_UNSUPPORTED);
  driverHal.busy = driverHal.failSetRx = false;
  assert(radio.transmit(data.data(), data.size()) == 0);
  std::puts("PASS: SetRx BUSY fault releases front end and staged mode");

  driverHal.failClearAfterTx = true;
  const int priorStandby = driverHal.standbyWrites;
  assert(radio.transmit(data.data(), data.size()) == RADIOLIB_ERR_SPI_CMD_INVALID);
  assert(driverHal.pins[9] == 0 && driverHal.pins[10] == 0);
  assert(driverHal.standbyWrites == priorStandby + 1);  // Entry only; no extra cleanup.
  assert(driverHal.irq != 0);  // Failed SPI cannot promise that silicon IRQ cleared.
  driverHal.failClearAfterTx = driverHal.clearIrqFault = false;
  assert(radio.transmit(data.data(), data.size()) == 0);
  assert(driverHal.irq == 0);
  std::puts("PASS: IRQ cleanup error is preserved; next successful operation clears IRQ");
  std::puts("PASS: real RadioLib 7.7.1 recovery regressions (simulated SPI/GPIO only)");
}
