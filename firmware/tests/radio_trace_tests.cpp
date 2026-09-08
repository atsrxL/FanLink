#include "fanlink/radio_trace.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
int main() {
  using namespace fanlink;
  RadioTraceRing ring; RadioTraceRecord row;
  assert(!ring.peek(row)); ring.pop();
  for (unsigned i = 0; i < 40; ++i) ring.record({UINT32_MAX - 20U + i, i, 0, RadioTraceEvent::IrqRead, 3, 15});
  ring.finish(RadioTraceKind::Crc);
  assert(ring.peek(row) && row.count == 32 && row.sample.value == 8 && row.historyOverwrites == 8);
  const auto initial = row;
  ring.record({20, 999, -5, RadioTraceEvent::FaultCleanup, 2, 11});
  ring.finish(RadioTraceKind::Io);
  assert(ring.dropped() == 1 && ring.peek(row) && row.sample.value == initial.sample.value && row.dropped == 0);
  std::uint32_t prior = row.sample.micros;
  for (unsigned i = 0; i < 32; ++i) {
    assert(ring.peek(row) && row.index == i && row.sample.value == i + 8 && row.generation == 1);
    if (i) assert(static_cast<std::uint32_t>(row.sample.micros - prior) == 1);
    prior = row.sample.micros; ring.pop();
  }
  assert(!ring.peek(row));
  for (unsigned i = 0; i < 63; ++i) ring.finish(RadioTraceKind::Good);
  assert(!ring.peek(row)); ring.finish(RadioTraceKind::Good);
  assert(ring.peek(row) && row.kind == RadioTraceKind::Good && row.generation == 2 && row.dropped == 1);
  char text[128];
  row = {UINT32_MAX, UINT32_MAX, UINT32_MAX, RadioTraceKind::Io, 31, 32,
      {UINT32_MAX, UINT32_MAX, INT16_MIN, RadioTraceEvent::FaultCleanup, 3, 15}};
  const auto size = formatRadioTrace(row, text, sizeof(text));
  assert(size > 0 && size < 118 && size == std::strlen(text));
  assert(std::strstr(text, "w4294967295") && std::strstr(text, "aFFFFFFFF r-32768"));
  char small[8]; assert(formatRadioTrace(row, small, sizeof(small)) == 0 && small[0] == 0);
  assert(formatRadioTrace(row, nullptr, 128) == 0);
  assert(formatRadioTrace(row, text, 0) == 0);
  std::puts("PASS: bounded radio trace ordering/wrap/freeze/drop/good sampling/formatter");
}
