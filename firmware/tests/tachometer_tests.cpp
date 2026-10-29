#include <Arduino.h>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include "fanlink/platform/esp8266/tachometer.hpp"

namespace {
std::uint32_t clockUs = 1000U;
void (*edgeHandler)() = nullptr;
bool masked = false;
unsigned checks = 0;
constexpr std::uint32_t timeoutUs = 1000000U;
using fanlink::platform::esp8266::Esp8266Tachometer;
void check(bool condition, const char* message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
void edgeAfter(std::uint32_t delta) {
  clockUs += delta;
  check(edgeHandler != nullptr && !masked, "ISR not attached or interrupts left masked");
  edgeHandler();
}
fanlink::TachSample sample(Esp8266Tachometer& tach) {
  // Caller time can legitimately precede an edge; adapter must take fresh time.
  const auto result = tach.sample(clockUs - 100U);
  check(!masked, "sample left interrupts masked");
  return result;
}
void expectRpm(Esp8266Tachometer& tach, unsigned expected, unsigned tolerance = 20U) {
  const auto result = sample(tach);
  check(result.valid, "expected valid tach sample");
  check(result.rpm + tolerance >= expected && result.rpm <= expected + tolerance,
        "RPM outside expected range");
}
void expectInvalid(Esp8266Tachometer& tach) {
  const auto result = sample(tach);
  check(!result.valid && result.rpm == 0U, "expected invalid zero RPM");
}
void train(Esp8266Tachometer& tach, std::uint32_t period, unsigned count = 16U) {
  for (unsigned i = 0; i < count; ++i) {
    edgeAfter(period);
    (void)sample(tach);
  }
}
void test(const std::string& name) {
  if (name == "config") {
    Esp8266Tachometer badPin(16, 2U, timeoutUs, 250U, 30000U);
    Esp8266Tachometer badPpr(1, 0U, timeoutUs, 250U, 30000U);
    Esp8266Tachometer badTimeout(1, 2U, 0U, 250U, 30000U);
    Esp8266Tachometer badRpm(1, 2U, timeoutUs, 250U, 0U);
    Esp8266Tachometer ambiguousTime(1, 2U, 0x80000000U, 250U, 30000U);
    Esp8266Tachometer impossiblePeriod(1, 2U, 1000U, 250U, 30000U);
    check(!badPin.begin(), "invalid pin accepted");
    check(!badPpr.begin(), "zero pulses/revolution accepted");
    check(!badTimeout.begin(), "zero timeout accepted");
    check(!badRpm.begin(), "zero maximum RPM accepted");
    check(!ambiguousTime.begin(), "ambiguous wrap interval accepted");
    check(!impossiblePeriod.begin(), "period at timeout accepted");
    check(edgeHandler == nullptr, "invalid config attached ISR");
    return;
  }
  if (name == "zero") clockUs = 0U;
  if (name == "wrap") clockUs = UINT32_MAX - 80000U;
  Esp8266Tachometer tach(1, 2U, timeoutUs, 250U, 30000U);
  check(!tach.diagnostics(clockUs).enabled, "uninitialized diagnostics enabled");
  expectInvalid(tach);
  check(tach.begin(), "begin failed");
  expectInvalid(tach);
  const auto empty = tach.diagnostics(clockUs);
  check(empty.enabled && empty.rawEdges == 0U && empty.validPeriods == 0U &&
        empty.filterRejects == 0U && empty.overflowDrops == 0U &&
        empty.lastEdgeAgeUs == UINT32_MAX, "empty diagnostics incorrect");
  if (name == "diagnostics") {
    // Snapshot before sampling sees ISR edges, but must not drain the ring or
    // manufacture accepted windows. First edge + four periods is only warmup.
    for (unsigned i = 0; i < 5U; ++i) edgeAfter(20000U);
    auto d = tach.diagnostics(clockUs - 100U);
    check(d.rawEdges == 5U && d.validPeriods == 0U && d.filterRejects == 0U &&
          d.lastEdgeAgeUs == 0U, "snapshot before drain incorrect");
    expectInvalid(tach);
    d = tach.diagnostics(clockUs);
    check(d.validPeriods == 0U && d.filterRejects == 0U, "warmup counted as rejection");
    edgeAfter(20000U);
    expectRpm(tach, 1500U, 1U);
    for (unsigned i = 0; i < 100U; ++i) {
      d = tach.diagnostics(clockUs);
      check(d.rawEdges == 6U && d.validPeriods == 1U && d.filterRejects == 0U,
            "read-only diagnostics changed counters");
    }
    clockUs += 123U;
    check(tach.diagnostics(clockUs).lastEdgeAgeUs == 123U, "raw age incorrect");
    edgeAfter(500U);
    expectRpm(tach, 1500U, 1U);
    d = tach.diagnostics(clockUs);
    check(d.rawEdges == 7U && d.validPeriods == 1U && d.filterRejects == 1U &&
          d.lastEdgeAgeUs == 0U, "short rejected raw edge not counted");
    // Saturating accumulated age survives multiple micros wraps with ordinary
    // loop sampling; new edge reestablishes age without clearing any counters.
    for (unsigned i = 0; i < 10U; ++i) {
      clockUs += 1000000000U;
      expectInvalid(tach);
    }
    check(tach.diagnostics(clockUs).lastEdgeAgeUs == UINT32_MAX - 1U,
          "quiet input became fresh after clock wrap");
    edgeAfter(100U);
    check(tach.diagnostics(clockUs).lastEdgeAgeUs == 0U, "new raw edge did not reset age");
    // Without Control packets sample() may not run at all: periodic diagnostic
    // snapshots alone must keep age across multiple micros wraps, without
    // consuming this queued edge or changing any estimator/counter state.
    const auto beforeQuiet = tach.diagnostics(clockUs);
    for (unsigned i = 0; i < 10U; ++i) {
      clockUs += 1000000000U;
      const auto quiet = tach.diagnostics(clockUs);
      check(quiet.rawEdges == beforeQuiet.rawEdges &&
            quiet.validPeriods == beforeQuiet.validPeriods &&
            quiet.filterRejects == beforeQuiet.filterRejects,
            "diagnostic-only age tick changed estimator counters");
    }
    check(tach.diagnostics(clockUs).lastEdgeAgeUs == UINT32_MAX - 1U,
          "diagnostic-only age tick lost quiet time across wrap");
    // This queued edge is now stale; sampling must still see and reject it.
    expectInvalid(tach);
    check(tach.diagnostics(clockUs).filterRejects == 2U,
          "diagnostic age tick consumed queued edge");
    // Capacity 31; dropped edges still count raw and update last raw age.
    for (unsigned i = 0; i < 40U; ++i) edgeAfter(500U);
    clockUs += 25U;
    d = tach.diagnostics(clockUs);
    check(d.rawEdges == 48U && d.overflowDrops == 9U && d.lastEdgeAgeUs == 25U,
          "overflow raw evidence incorrect");
    check(d.filterRejects == 2U, "snapshot consumed overflow");
    expectInvalid(tach);
    check(tach.diagnostics(clockUs).filterRejects == 3U, "overflow batch not counted once");
    expectInvalid(tach);
    check(tach.diagnostics(clockUs).filterRejects == 3U, "repeat read counted overflow twice");
    train(tach, 20000U);
    expectRpm(tach, 1500U, 1U);
    return;
  }
  edgeAfter(0U);
  expectInvalid(tach);
  if (name == "acquisition" || name == "zero") {
    // A lone plausible noise interval cannot establish RPM. Re-reading the
    // same two edges must not manufacture independent observations.
    edgeAfter(20000U);
    for (unsigned i = 0; i < 100; ++i) expectInvalid(tach);
    edgeAfter(20000U);
    expectInvalid(tach);
    edgeAfter(20000U);
    expectInvalid(tach);
    edgeAfter(20000U);
    expectInvalid(tach);
    edgeAfter(20000U);
    expectRpm(tach, 1500U, 1U);
  } else if (name == "stable1400") {
    train(tach, 21429U);
    expectRpm(tach, 1400U, 2U);
  } else if (name == "wrap" || name == "stable1500") {
    train(tach, 20000U);
    expectRpm(tach, 1500U, 1U);
  } else {
    train(tach, 20000U);
    expectRpm(tach, 1500U, 1U);
    if (name == "isolated500" || name == "isolated5000") {
      const std::uint32_t noiseOffset = name == "isolated500" ? 500U : 5000U;
      edgeAfter(noiseOffset);
      expectRpm(tach, 1500U);
      edgeAfter(20000U - noiseOffset);
      expectRpm(tach, 1500U);
      train(tach, 20000U);
      expectRpm(tach, 1500U);
    } else if (name == "burst") {
      // Short burst between real edges, followed by real edges. Never publish
      // a transient zero or many-thousand RPM from this isolated disturbance.
      for (unsigned i = 0; i < 6; ++i) {
        edgeAfter(500U);
        expectRpm(tach, 1500U, 100U);
      }
      edgeAfter(17000U);
      train(tach, 20000U);
      expectRpm(tach, 1500U);
    } else if (name == "missing") {
      edgeAfter(40000U);
      expectRpm(tach, 1500U, 100U);
      train(tach, 20000U);
      expectRpm(tach, 1500U);
    } else if (name == "step") {
      train(tach, 10000U, 12U);
      expectRpm(tach, 3000U);
      train(tach, 40000U, 12U);
      expectRpm(tach, 750U);
      train(tach, 21429U, 12U);
      expectRpm(tach, 1400U);
    } else if (name == "stop" || name == "restart") {
      // Repeated polling must not count the same window as new observations
      // or extend freshness. Last valid value has a strict bounded lifetime.
      for (unsigned i = 0; i < 90; ++i) {
        clockUs += 10000U;
        expectRpm(tach, 1500U);
      }
      clockUs += 100001U;
      expectInvalid(tach);
      if (name == "restart") {
        train(tach, 10000U);
        expectRpm(tach, 3000U);
      }
    } else if (name == "noise_age") {
      // 60,000 RPM-equivalent out-of-range chatter cannot keep an old valid
      // estimate alive indefinitely. This does not claim arbitrary periodic
      // in-range electrical noise is distinguishable from a real tach signal.
      for (unsigned i = 0; i < 2200; ++i) {
        edgeAfter(500U);
        (void)sample(tach);
      }
      expectInvalid(tach);
      train(tach, 20000U);
      expectRpm(tach, 1500U);
    } else if (name == "stalled_loop") {
      // ISR capture can overrun a bounded ring while the main loop is busy.
      // Recovery must be memory-safe and converge using new real periods.
      for (unsigned i = 0; i < 1000; ++i) edgeAfter(500U);
      expectInvalid(tach);
      train(tach, 20000U);
      expectRpm(tach, 1500U);
    } else if (name == "queued_stale") {
      const auto previousRejects = tach.diagnostics(clockUs).filterRejects;
      for (unsigned i = 0; i < 8; ++i) edgeAfter(20000U);
      clockUs += timeoutUs;
      expectInvalid(tach);
      check(tach.diagnostics(clockUs).filterRejects == previousRejects + 8U,
            "stale queued edges not counted");
      train(tach, 20000U);
      expectRpm(tach, 1500U);
    } else if (name == "queue_capacity") {
      for (unsigned i = 0; i < 31; ++i) edgeAfter(20000U);
      expectRpm(tach, 1500U);
      for (unsigned i = 0; i < 32; ++i) edgeAfter(20000U);
      expectInvalid(tach);
      train(tach, 20000U);
      expectRpm(tach, 1500U);
    } else if (name == "max_speed") {
      train(tach, 1000U);
      expectRpm(tach, 30000U, 1U);
      train(tach, 20000U);
      expectRpm(tach, 1500U);
    } else if (name == "recurring_noise") {
      const auto beforeNoise = tach.diagnostics(clockUs);
      for (unsigned i = 0; i < 60U; ++i) {
        for (const auto interval : {5000U, 15000U}) {
          edgeAfter(interval);
          const auto result = sample(tach);
          check(!result.valid || (result.rpm >= 1480U && result.rpm <= 1520U),
                "alternating extra edges published a bogus RPM");
        }
      }
      expectInvalid(tach);
      const auto afterNoise = tach.diagnostics(clockUs);
      check(afterNoise.rawEdges == beforeNoise.rawEdges + 120U &&
            afterNoise.filterRejects > beforeNoise.filterRejects,
            "consensus rejected windows not counted");
      train(tach, 20000U);
      expectRpm(tach, 1500U);
    } else {
      throw std::runtime_error("unknown scenario");
    }
  }
}
}  // namespace
std::uint32_t micros() { return clockUs; }
void pinMode(int pin, int mode) { check(pin == 1 && mode == INPUT_PULLUP, "wrong tach pin mode"); }
int digitalPinToInterrupt(int pin) { return pin; }
void attachInterrupt(int pin, void (*handler)(), int mode) {
  check(pin == 1 && mode == FALLING, "wrong tach interrupt registration");
  edgeHandler = handler;
}
void noInterrupts() { check(!masked, "nested interrupt masking"); masked = true; }
void interrupts() { check(masked, "unbalanced interrupt restore"); masked = false; }
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  try {
    test(argv[1]);
    std::cout << "PASS: tachometer " << argv[1] << " " << checks << " checks\n";
  } catch (const std::exception& error) {
    std::cerr << "FAIL: tachometer " << argv[1] << ": " << error.what() << '\n';
    return 1;
  }
}
