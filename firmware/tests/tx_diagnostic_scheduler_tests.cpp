#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>

#include "fanlink/serial_protocol.hpp"
#include "fanlink/tx_diagnostic_text.hpp"
#include "fanlink/tx_diagnostic_scheduler.hpp"

namespace {
// Core 3.1.2 UART_TX_FIFO_SIZE is 0x80. 115200 baud, 8N1: ten bits
// per byte. Keep fractional drain time, including a partially sent byte.
struct Fifo {
  std::uint64_t work{0};
  std::uint64_t atUs{0};
  void drain(std::uint64_t nowUs) {
    const auto drained = (nowUs - atUs) * 115200U;
    work = drained < work ? work - drained : 0U;
    atUs = nowUs;
  }
  int available() const {
    return 128 - static_cast<int>((work + 9999999U) / 10000000U);
  }
  int availableForWrite() const { return available(); }
  std::size_t write(const std::uint8_t* data, std::size_t length) {
    fanlink::SerialFrameParser parser;
    fanlink::SerialFrame decoded{};
    bool completed = false;
    for (std::size_t i = 0; i < length; ++i) completed = parser.feed(data[i], decoded);
    assert(completed && decoded.type == fanlink::serial_type::kErrorEvent);
    assert(decoded.payloadLength + 10U == length);
    assert(write(length));
    return length;
  }
  bool write(std::size_t length) {
    if (!fanlink::txDiagnosticFrameCanWrite(length, available())) return false;
    work += length * 10000000U;
    assert(available() > 0);
    return true;
  }
};

// RFR, RFN, RFT, RF, RFC, RFE: use production formatters and binary
// encoding, not guessed payload or framing lengths.
std::array<std::size_t, 6> representativeLengths() {
  fanlink::TxNodeDiagnostics node{};
  node.transmittedPolls = 600; node.acceptedReplies = 471;
  node.receiveTimeouts = 129; node.receiveCrcErrors = 119;
  node.observedReplies = 471; node.lastObservedOffsetUs = 1900;
  node.lastCompletedOffsetUs = 2500; node.lastReceiveStartedOffsetUs = 480;
  node.receiveErrors.events = node.receiveErrors.irqCrc = node.receiveErrors.packetCrc = 117;
  auto& event = node.receiveErrors.last;
  event.kind = 1; event.sequence = 4800; event.validity = 15; event.errorFlags = 6;
  event.irq = 0x42; event.packetStatusError = 0x10; event.packetStatusSync = 1;
  event.errorRssiDbm = -21; event.observedOffsetUs = 1900; event.receiveStartedOffsetUs = 480;
  fanlink::NodeTelemetry telemetry{};
  telemetry.seen = true; telemetry.status = 6; telemetry.lastSeenMs = 747;
  fanlink::TxRadioDiagnostics radio{};
  radio.beginOk = true; radio.lastNodeId = 2; radio.lastSequence = 4800;
  radio.acceptedReplies = 471; radio.receiveTimeouts = 129; radio.receiveCrcErrors = 119;
  std::array<std::size_t, 6> lengths{};
  for (std::size_t i = 0; i < lengths.size(); ++i) {
    char message[fanlink::kSerialMaxPayload + 1]{};
    std::size_t count = 0;
    switch (i) {
      case 0: count = fanlink::formatTxRadioRecoveryDiagnostics(radio, message, sizeof(message)); break;
      case 1: count = fanlink::formatTxNodeDiagnostics(2, telemetry, node, 1000, message, sizeof(message)); break;
      case 2: count = fanlink::formatTxTimingDiagnostics(2, node, message, sizeof(message)); break;
      case 3: count = fanlink::formatTxRadioDiagnostics(radio, message, sizeof(message)); break;
      case 4: count = fanlink::formatTxReceiveErrorCounts(2, node, message, sizeof(message)); break;
      case 5: count = fanlink::formatTxReceiveErrorEvent(2, node, message, sizeof(message)); break;
    }
    assert(count != 0);
    fanlink::SerialFrame frame{};
    frame.type = fanlink::serial_type::kErrorEvent;
    frame.payloadLength = static_cast<std::uint16_t>(count);
    std::array<std::uint8_t, fanlink::kSerialMaxFrameSize> encoded{};
    assert(fanlink::encodeSerialFrame(frame, encoded, lengths[i]));
    assert(lengths[i] == count + 10U);
  }
  return lengths;
}

void reproduceLegacyStarvation() {
  const auto lengths = representativeLengths();
  Fifo fifo;
  std::array<std::uint32_t, 6> last{};
  std::array<unsigned, 6> sent{};
  std::uint32_t lastStatus = 0, lastMetrics = 0;
  std::uint64_t lastPoll = 0;
  unsigned polls = 0;
  for (std::uint64_t us = 0; us < 60000000U; us += 100U) {
    if (us - lastPoll >= 125000U) {
      lastPoll = us; ++polls; us += 5000U; // bounded RF work before diagnostics
    }
    fifo.drain(us);
    const auto ms = static_cast<std::uint32_t>(us / 1000U);
    if (ms - lastStatus >= 250U) {
      if (fifo.write(112)) lastStatus = ms;
      continue; // STATUS blocks all lower priority even when FIFO is full
    }
    if (ms - lastMetrics >= 250U) {
      if (fifo.write(42)) lastMetrics = ms;
      continue;
    }
    for (std::size_t i = 0; i < lengths.size(); ++i) {
      if (i == 0 || ms - last[i] < 1000U) continue; // unchanged recovery counter
      last[i] = ms; // legacy defect: consume the period BEFORE a blocked write
      if (fifo.write(lengths[i])) { ++sent[i]; break; }
    }
  }
  assert(polls == 479);
  std::cout << "legacy 60s (RFR RFN RFT RF RFC RFE):";
  for (auto count : sent) std::cout << ' ' << count;
  std::cout << '\n';
  assert(sent[1] == 0 && sent[2] == 0 && sent[5] == 0);
}

void testFairFifo(std::uint32_t stepUs, bool allChanging, bool maximumLengths,
                  std::uint32_t originMs = 0U, bool maintenancePause = false, bool traceSaturated = false) {
  const auto legacyLengths = representativeLengths();
  std::array<std::size_t, 7> lengths{};
  std::copy(legacyLengths.begin(), legacyLengths.end(), lengths.begin());
  lengths[6] = 127U; // worst writable trace fragment, continuously pending
  // The longest possible writable optional frame leaves the required spare
  // byte; a 128+ byte record must be dropped without blocking other families.
  if (maximumLengths) lengths = {127U, 123U, 127U, 138U, 127U, 127U, 127U};
  Fifo fifo;
  fanlink::TxDiagnosticScheduler scheduler;
  scheduler.reset(originMs);
  std::array<unsigned, 7> sent{}, dropped{};
  std::array<std::uint32_t, 7> lastSent{}, maxGap{};
  lastSent.fill(originMs);
  std::uint32_t lastStatus = originMs, lastMetrics = originMs;
  std::uint32_t maxStatusGap = 0, maxMetricsGap = 0;
  std::uint16_t sequence = 0;
  std::uint64_t lastPoll = 0;
  unsigned polls = 0, statusCount = 0, metricsCount = 0;
  for (std::uint64_t us = 0; us < 60000000U; us += stepUs) {
    // Matches production's early return while maintenance owns service.
    if (maintenancePause && us >= 10000000U && us < 13000000U) continue;
    if (us - lastPoll >= 125000U) {
      if (!(maintenancePause && us - lastPoll > 1000000U))
        assert(us - lastPoll < 125000U + stepUs);
      lastPoll = us; ++polls; us += 5000U;
    }
    fifo.drain(us);
    const auto ms = originMs + static_cast<std::uint32_t>(us / 1000U);
    if (static_cast<std::uint32_t>(ms - lastStatus) >= 250U) {
      if (fifo.write(112)) {
        maxStatusGap = std::max(maxStatusGap, static_cast<std::uint32_t>(ms - lastStatus));
        lastStatus = ms; ++statusCount;
      }
      continue;
    }
    if (static_cast<std::uint32_t>(ms - lastMetrics) >= 250U) {
      if (fifo.write(42)) {
        maxMetricsGap = std::max(maxMetricsGap, static_cast<std::uint32_t>(ms - lastMetrics));
        lastMetrics = ms; ++metricsCount;
      }
      continue;
    }
    unsigned checks = 0, attempts = 0, writes = 0;
    scheduler.service(ms, [&](fanlink::TxDiagnosticFamily family) {
      ++checks;
      const auto i = static_cast<std::size_t>(family);
      if (i == 6 && !traceSaturated) return fanlink::TxDiagnosticAttempt::NotReady;
      if (i == 0 && !allChanging) return fanlink::TxDiagnosticAttempt::NotReady;
      ++attempts;
      // Exercise the SAME writer used by main_tx, including encoded length,
      // FIFO spare-byte test, drop-vs-retry and SerialFrame parser readback.
      const std::array<char, fanlink::kSerialMaxPayload> payload{};
      const auto result = fanlink::writeTxOptionalDiagnostic(
          fifo, payload.data(), lengths[i] - 10U, sequence);
      if (result == fanlink::TxDiagnosticAttempt::Dropped) { ++dropped[i]; return result; }
      if (result == fanlink::TxDiagnosticAttempt::Blocked) return result;
      ++writes; ++sent[i];
      const auto gap = static_cast<std::uint32_t>(ms - lastSent[i]);
      assert(gap >= (i == 6 ? 25U : 1000U));
      maxGap[i] = std::max(maxGap[i], gap);
      lastSent[i] = ms;
      return fanlink::TxDiagnosticAttempt::Sent;
    });
    assert(checks <= 7 && attempts <= 1 && writes <= 1);
  }
  assert(polls >= (maintenancePause ? 450U : 470U));
  assert(statusCount >= (maintenancePause ? 220U : 230U));
  assert(metricsCount >= (maintenancePause ? 220U : 230U));
  assert(maxStatusGap <= (maintenancePause ? 3300U : 270U));
  assert(maxMetricsGap <= (maintenancePause ? 3300U : 270U));
  std::cout << "fair step=" << stepUs << "us max=" << maximumLengths
            << " all=" << allChanging << " wrap=" << (originMs != 0U)
            << " pause=" << maintenancePause << " trace=" << traceSaturated << " sent:";
  for (std::size_t i = 0; i < sent.size(); ++i) {
    std::cout << ' ' << sent[i];
    if (i == 6) {
      if (!traceSaturated) { assert(sent[i] == 0); continue; }
      assert(sent[i] >= (maintenancePause ? 1600U : 1800U));
      assert(maxGap[i] <= (maintenancePause ? 3200U : 200U));
      assert(static_cast<std::uint32_t>(originMs + 60000U - lastSent[i]) <= 200U);
      continue;
    }
    if (i == 0 && !allChanging) { assert(sent[i] == 0); continue; }
    if (lengths[i] >= 128U) { assert(sent[i] == 0 && dropped[i] >= 50U); continue; }
    assert(sent[i] >= (maintenancePause ? 50U : 55U));
    assert(maxGap[i] <= (maintenancePause ? 4200U : 1150U));
    // Include tail silence in the fairness check, not only successful sends.
    assert(static_cast<std::uint32_t>(originMs + 60000U - lastSent[i]) <= 1150U);
  }
  std::cout << " max-gap-ms:";
  for (auto gap : maxGap) std::cout << ' ' << gap;
  std::cout << " status/metrics=" << statusCount << '/' << metricsCount
            << " gap=" << maxStatusGap << '/' << maxMetricsGap << '\n';
}

void testStateTransitions() {
  using fanlink::TxDiagnosticAttempt;
  using fanlink::TxDiagnosticFamily;
  fanlink::TxDiagnosticScheduler scheduler;
  scheduler.reset(UINT32_MAX - 500U);
  unsigned calls = 0;
  auto countNotReady = [&](TxDiagnosticFamily family) {
    if (family != TxDiagnosticFamily::Trace) ++calls;
    return TxDiagnosticAttempt::NotReady;
  };
  scheduler.service(498U, countNotReady);
  assert(calls == 0); // 999ms across wrap: nothing due
  scheduler.service(499U, countNotReady);
  assert(calls == 6); // exactly one bounded scan, no ready data
  calls = 0;
  for (unsigned i = 0; i < 10; ++i) {
    scheduler.service(499U, [&](TxDiagnosticFamily family) {
      ++calls; assert(family == TxDiagnosticFamily::Recovery);
      return TxDiagnosticAttempt::Blocked;
    });
  }
  assert(calls == 10); // blocked FIFO neither spends period nor advances cursor
  scheduler.service(500U, [&](TxDiagnosticFamily family) {
    assert(family == TxDiagnosticFamily::Recovery); return TxDiagnosticAttempt::Sent;
  });
  scheduler.service(500U, [&](TxDiagnosticFamily family) {
    assert(family == TxDiagnosticFamily::Node); return TxDiagnosticAttempt::Dropped;
  });
  scheduler.service(500U, [&](TxDiagnosticFamily family) {
    assert(family == TxDiagnosticFamily::Timing); return TxDiagnosticAttempt::Sent;
  });
  calls = 0;
  scheduler.service(500U, [&](TxDiagnosticFamily family) {
    assert(family != TxDiagnosticFamily::Recovery && family != TxDiagnosticFamily::Node &&
           family != TxDiagnosticFamily::Timing);
    if (family != TxDiagnosticFamily::Trace) ++calls;
    return TxDiagnosticAttempt::NotReady;
  });
  assert(calls == 3); // sent/dropped families retain per-family one-second throttle
}

void testTracePeriodAndBlockedWrap() {
  using fanlink::TxDiagnosticAttempt;
  using fanlink::TxDiagnosticFamily;
  fanlink::TxDiagnosticScheduler scheduler;
  scheduler.reset(UINT32_MAX - 10U);
  unsigned calls = 0;
  auto blocked = [&](TxDiagnosticFamily family) {
    assert(family == TxDiagnosticFamily::Trace); ++calls;
    return TxDiagnosticAttempt::Blocked;
  };
  scheduler.service(13U, blocked);
  assert(calls == 0); // 24ms across wrap
  scheduler.service(14U, blocked);
  scheduler.service(14U, blocked);
  assert(calls == 2); // 25ms due; full FIFO retains due trace and cursor
  scheduler.service(15U, [&](TxDiagnosticFamily family) {
    assert(family == TxDiagnosticFamily::Trace);
    return TxDiagnosticAttempt::Sent;
  });
  scheduler.service(39U, blocked);
  assert(calls == 2); // throttle measured from actual send, not original due time
  scheduler.service(40U, blocked);
  assert(calls == 3);
}

void testWriterBounds() {
  using fanlink::TxDiagnosticAttempt;
  Fifo fifo;
  std::array<char, fanlink::kSerialMaxPayload> payload{};
  std::uint16_t sequence = UINT16_MAX;
  assert(fanlink::writeTxOptionalDiagnostic(fifo, nullptr, 1U, sequence) == TxDiagnosticAttempt::Dropped);
  assert(fanlink::writeTxOptionalDiagnostic(fifo, payload.data(), 0U, sequence) == TxDiagnosticAttempt::Dropped);
  assert(fanlink::writeTxOptionalDiagnostic(fifo, payload.data(), 118U, sequence) == TxDiagnosticAttempt::Dropped);
  assert(fanlink::writeTxOptionalDiagnostic(fifo, payload.data(), 129U, sequence) == TxDiagnosticAttempt::Dropped);
  assert(sequence == UINT16_MAX && fifo.available() == 128);
  assert(fanlink::writeTxOptionalDiagnostic(fifo, payload.data(), 117U, sequence) == TxDiagnosticAttempt::Sent);
  assert(sequence == 0 && fifo.available() == 1);
  assert(fanlink::writeTxOptionalDiagnostic(fifo, payload.data(), 1U, sequence) == TxDiagnosticAttempt::Blocked);
  assert(sequence == 0 && fifo.available() == 1);
  fifo.drain(11025U);
  assert(fifo.available() == 128);
  assert(fanlink::writeTxOptionalDiagnostic(fifo, payload.data(), 1U, sequence) == TxDiagnosticAttempt::Sent);
  assert(sequence == 1);
  struct ShortSerial {
    unsigned writes{0};
    int free{11};
    int availableForWrite() const { return free; }
    std::size_t write(const std::uint8_t*, std::size_t length) { ++writes; return length - 1U; }
  } shortSerial;
  // The final reported FIFO byte must not be filled, even for tiny frames.
  assert(fanlink::writeTxOptionalDiagnostic(shortSerial, payload.data(), 1U, sequence) == TxDiagnosticAttempt::Blocked);
  assert(shortSerial.writes == 0 && sequence == 1);
  shortSerial.free = 12;
  assert(fanlink::writeTxOptionalDiagnostic(shortSerial, payload.data(), 1U, sequence) == TxDiagnosticAttempt::Dropped);
  assert(shortSerial.writes == 1 && sequence == 2); // no duplicate retry after partial output
}
}  // namespace

int main() {
  reproduceLegacyStarvation();
  testStateTransitions();
  testTracePeriodAndBlockedWrap();
  testWriterBounds();
  for (auto step : {100U, 1000U, 3700U}) {
    testFairFifo(step, false, false);
    testFairFifo(step, true, false);
    testFairFifo(step, true, true);
  }
  testFairFifo(100U, true, false, UINT32_MAX - 500U);
  testFairFifo(100U, true, true, UINT32_MAX - 500U, true);
  for (auto step : {100U, 1000U, 3700U})
    testFairFifo(step, true, true, UINT32_MAX - 500U, false, true);
  testFairFifo(100U, true, true, UINT32_MAX - 500U, true, true);
  std::cout << "PASS: TX diagnostic FIFO starvation/fairness regression\n";
}
