#pragma once
#include <algorithm>
#include <array>
#include "fanlink/byte_utils.hpp"
#include "fanlink/crc16.hpp"
#include "fanlink/interfaces.hpp"

namespace fanlink::tachdiag {
inline constexpr std::size_t kSize = 30;
inline constexpr std::uint8_t kRequest = 0x42, kReply = 0x43;
inline constexpr std::uint8_t kHostRequest = 0x16, kHostReply = 0x94;
struct Request { std::uint32_t network{0}, session{0}; std::uint8_t node{0}; };
struct Reply { Request request{}; bool enabled{false};
  std::uint32_t rawEdges{0}, validPeriods{0}, filterRejects{0};
  std::uint16_t lastEdgeAgeMs{65535}; };
using Packet = std::array<std::uint8_t, kSize>;
inline bool valid(const Request& r) { return r.session && r.node >= 1 && r.node <= 8; }
inline bool same(const Request& a, const Request& b) {
  return a.network == b.network && a.session == b.session && a.node == b.node;
}
inline bool envelope(const std::uint8_t* p, std::size_t n, std::uint8_t type) {
  return p && n == kSize && p[0] == 0xf1 && p[1] == 0xa5 && p[2] == 1 &&
      p[3] == type && bytes::getU16Le(p + 28) == crc16CcittFalse(p, 28);
}
inline Request identity(const std::uint8_t* p) {
  return {bytes::getU32Le(p + 4), bytes::getU32Le(p + 8), p[12]};
}
inline void header(const Request& r, std::uint8_t type, Packet& p) {
  p.fill(0); p[0] = 0xf1; p[1] = 0xa5; p[2] = 1; p[3] = type;
  bytes::putU32Le(p.data() + 4, r.network); bytes::putU32Le(p.data() + 8, r.session);
  p[12] = r.node;
}
inline void seal(Packet& p) { bytes::putU16Le(p.data() + 28, crc16CcittFalse(p.data(), 28)); }
inline bool encodeRequest(const Request& r, Packet& p) {
  if (!valid(r)) return false;
  header(r, kRequest, p); seal(p); return true;
}
inline bool decodeRequest(const std::uint8_t* p, std::size_t n, Request& r) {
  if (!envelope(p, n, kRequest)) return false;
  const auto candidate = identity(p); if (!valid(candidate)) return false;
  for (std::size_t i = 13; i < 28; ++i) if (p[i]) return false;
  r = candidate; return true;
}
inline bool encodeReply(const Reply& r, Packet& p) {
  if (!valid(r.request)) return false;
  header(r.request, kReply, p); p[13] = r.enabled ? 1 : 0;
  bytes::putU32Le(p.data() + 14, r.rawEdges);
  bytes::putU32Le(p.data() + 18, r.validPeriods);
  bytes::putU32Le(p.data() + 22, r.filterRejects);
  bytes::putU16Le(p.data() + 26, r.lastEdgeAgeMs); seal(p); return true;
}
inline bool decodeReply(const std::uint8_t* p, std::size_t n, Reply& r) {
  if (!envelope(p, n, kReply) || !valid(identity(p)) || (p[13] & ~1U)) return false;
  r = {identity(p), p[13] != 0, bytes::getU32Le(p + 14),
      bytes::getU32Le(p + 18), bytes::getU32Le(p + 22), bytes::getU16Le(p + 26)};
  return true;
}
// Read-only wrapper: never activates maintenance or refreshes control freshness.
class RxMaintenance final : public IRxMaintenance {
 public:
  RxMaintenance(IRxMaintenance& delegate, ITachometer& tach, IClock& clock,
                std::uint32_t network, std::uint8_t node)
      : delegate_(delegate), tach_(tach), clock_(clock), network_(network), node_(node) {}
  void tick(std::uint32_t now) override { delegate_.tick(now); }
  bool active() const override { return delegate_.active(); }
  bool replyRequiresFullSpeed() const override {
    return !readOnlyReply_ && delegate_.replyRequiresFullSpeed();
  }
  bool handle(const std::uint8_t* data, std::size_t n, std::uint8_t* reply,
              std::size_t& length, std::uint32_t now) override {
    readOnlyReply_ = false; length = 0; Request r{};
    if (!decodeRequest(data, n, r)) return delegate_.handle(data, n, reply, length, now);
    if (r.network != network_ || r.node != node_ || active()) return true;
    const auto d = tach_.diagnostics(clock_.micros());
    const auto age = d.lastEdgeAgeUs == UINT32_MAX ? 65535U :
        std::min<std::uint32_t>(65534U, d.lastEdgeAgeUs / 1000U);
    Packet p{}; encodeReply({r, d.enabled, d.rawEdges, d.validPeriods, d.filterRejects,
                            static_cast<std::uint16_t>(age)}, p);
    std::copy(p.begin(), p.end(), reply); length = p.size(); readOnlyReply_ = true;
    return true;
  }
 private:
  IRxMaintenance& delegate_; ITachometer& tach_; IClock& clock_;
  std::uint32_t network_; std::uint8_t node_; bool readOnlyReply_{false};
};

// One explicitly requested exchange, started after a normal control slot.
// No retries, no EEPROM operations and no duty changes. Host control remains live.
class TxBridge {
 public:
  static constexpr std::uint32_t kWindowUs = 20000, kCooldownMs = 1000;
  TxBridge(IRadioPort& radio, IClock& clock, std::uint32_t network)
      : radio_(radio), clock_(clock), network_(network) {}
  bool enqueue(const std::uint8_t* p, std::size_t n, std::uint16_t seq, bool busy) {
    Request r{};
    if (busy || active() || haveReply_ || !decodeRequest(p, n, r) || r.network != network_ ||
        (haveStarted_ && std::uint32_t(clock_.millis() - lastStartedMs_) < kCooldownMs)) return false;
    request_ = r; sequence_ = seq; queued_ = true; queuedMs_ = clock_.millis(); return true;
  }
  void startAfterSlot() {
    if (!queued_) return;
    queued_ = false; haveStarted_ = true; lastStartedMs_ = clock_.millis();
    Packet p{}; encodeRequest(request_, p);
    if (radio_.transmit(p.data(), p.size()) != RadioResult::Ok) return;
    startedUs_ = clock_.micros(); waiting_ = true;
  }
  void service() {
    if (queued_ && std::uint32_t(clock_.millis() - queuedMs_) >= 250) queued_ = false;
    if (haveReply_ && std::uint32_t(clock_.millis() - lastStartedMs_) >= kCooldownMs) haveReply_ = false;
    if (!waiting_) return;
    const auto elapsed = std::uint32_t(clock_.micros() - startedUs_);
    if (elapsed >= kWindowUs) { waiting_ = false; return; }
    Packet p{}; const auto rx = radio_.receiveForUs(p.data(), p.size(),
        std::min<std::uint32_t>(1000, kWindowUs - elapsed));
    Reply r{};
    if (std::uint32_t(clock_.micros() - startedUs_) >= kWindowUs) { waiting_ = false; return; }
    if (rx.result == RadioResult::Ok && decodeReply(p.data(), rx.length, r) && same(r.request, request_)) {
      reply_ = p; haveReply_ = true; waiting_ = false;
    }
  }
  bool replyReady(Packet& p, std::uint16_t& seq) const {
    if (!haveReply_) return false;
    p = reply_; seq = sequence_; return true;
  }
  void replyWritten(bool ok) { if (ok) haveReply_ = false; }
  bool active() const { return queued_ || waiting_; }
  bool waiting() const { return waiting_; }
 private:
  IRadioPort& radio_; IClock& clock_; const std::uint32_t network_;
  Request request_{}; Packet reply_{}; std::uint16_t sequence_{0};
  std::uint32_t startedUs_{0}, lastStartedMs_{0}, queuedMs_{0};
  bool queued_{false}, waiting_{false}, haveReply_{false}, haveStarted_{false};
};
} // namespace fanlink::tachdiag
