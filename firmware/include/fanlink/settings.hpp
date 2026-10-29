#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include "fanlink/crc16.hpp"

namespace fanlink::settings {
inline constexpr std::size_t kPacketSize = 30;
inline constexpr std::size_t kRecordSize = 24;
inline constexpr std::uint32_t kApplyGraceMs = 2500;
enum class Profile : std::uint8_t { Lite = 1, Nano = 2 };
enum class PinMode : std::uint8_t { Uart = 0, Pwm = 1, PwmSwap = 2, Safe = 254, NotApplicable = 255 };
enum class Operation : std::uint8_t { Query = 1, SetPinMode = 2, SetPower = 3 };
enum class Status : std::uint8_t { Ok = 0, BadRequest = 1, Unsupported = 2, Busy = 3, StorageError = 4, RadioError = 5 };
struct Request {
  std::uint32_t networkId{0}, session{0};
  std::uint8_t nodeId{0};
  Operation operation{Operation::Query};
  std::uint8_t value{0};
};
struct Selection { PinMode mode{PinMode::Safe}; std::int8_t powerDbm{-18}; };
struct Reply {
  Request request{};
  Status status{Status::Ok};
  Profile profile{Profile::Lite};
  PinMode mode{PinMode::NotApplicable};
  std::int8_t powerDbm{-18};
  std::uint8_t flags{0};
  std::uint32_t capabilities{0};
};
inline bool validPower(Profile profile, std::int8_t power) {
  return profile == Profile::Lite ? power >= -18 && power <= 13 :
      profile == Profile::Nano && (power == -10 || power == -6 || power == -3 || power == 1);
}
inline bool validMode(PinMode mode) { return static_cast<unsigned>(mode) <= 2; }
inline void put32(std::uint8_t* p, std::uint32_t v) {
  for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<std::uint8_t>(v >> (8 * i));
}
inline std::uint32_t get32(const std::uint8_t* p) {
  std::uint32_t v = 0; for (unsigned i = 0; i < 4; ++i) v |= std::uint32_t(p[i]) << (8 * i); return v;
}
inline bool sameRequest(const Request& a, const Request& b) {
  return a.networkId == b.networkId && a.session == b.session && a.nodeId == b.nodeId &&
      a.operation == b.operation && a.value == b.value;
}
inline bool validRequest(const Request& r) {
  return r.session != 0 && r.nodeId <= 8 &&
      (r.operation == Operation::Query ? r.value == 0 :
       r.operation == Operation::SetPinMode || r.operation == Operation::SetPower);
}
inline void seal(std::uint8_t* b, std::size_t n) {
  const auto crc = crc16CcittFalse(b, n - 2); b[n - 2] = crc & 255; b[n - 1] = crc >> 8;
}
inline bool crcValid(const std::uint8_t* b, std::size_t n) {
  return crc16CcittFalse(b, n - 2) == (std::uint16_t(b[n - 2]) | std::uint16_t(b[n - 1]) << 8);
}
inline bool encodeRequest(const Request& r, std::array<std::uint8_t, kPacketSize>& b) {
  if (!validRequest(r)) return false;
  b.fill(0); b[0] = 0xF1; b[1] = 0xA5; b[2] = 1; b[3] = 0x40;
  put32(b.data() + 4, r.networkId); put32(b.data() + 8, r.session);
  b[12] = r.nodeId; b[13] = static_cast<std::uint8_t>(r.operation); b[14] = r.value;
  seal(b.data(), b.size()); return true;
}
inline bool decodeRequest(const std::uint8_t* b, std::size_t n, Request& r) {
  if (!b || n != kPacketSize || b[0] != 0xF1 || b[1] != 0xA5 || b[2] != 1 ||
      b[3] != 0x40 || !crcValid(b, n)) return false;
  for (unsigned i = 15; i < 28; ++i) if (b[i]) return false;
  r = {get32(b + 4), get32(b + 8), b[12], static_cast<Operation>(b[13]), b[14]};
  return validRequest(r);
}
inline bool encodeReply(const Reply& r, std::array<std::uint8_t, kPacketSize>& b) {
  if (!encodeRequest(r.request, b)) return false;
  b[3] = 0x41; b[15] = static_cast<std::uint8_t>(r.status);
  b[16] = static_cast<std::uint8_t>(r.profile); b[17] = static_cast<std::uint8_t>(r.mode);
  b[18] = static_cast<std::uint8_t>(r.powerDbm); b[19] = r.flags;
  put32(b.data() + 20, r.capabilities); b[24] = 0; b[25] = 20; b[26] = 8; b[27] = 125;
  seal(b.data(), b.size()); return true;
}
inline bool decodeReply(const std::uint8_t* b, std::size_t n, Reply& r) {
  if (!b || n != kPacketSize || b[0] != 0xF1 || b[1] != 0xA5 || b[2] != 1 ||
      b[3] != 0x41 || !crcValid(b, n) || b[15] > 5 || b[16] < 1 || b[16] > 2 ||
      (b[19] & ~3U) || b[24] != 0 || b[25] != 20 || b[26] != 8 || b[27] != 125) return false;
  r.request = {get32(b + 4), get32(b + 8), b[12], static_cast<Operation>(b[13]), b[14]};
  r.status = static_cast<Status>(b[15]); r.profile = static_cast<Profile>(b[16]);
  r.mode = static_cast<PinMode>(b[17]); r.powerDbm = static_cast<std::int8_t>(b[18]);
  r.flags = b[19]; r.capabilities = get32(b + 20);
  return validRequest(r.request) && validPower(r.profile, r.powerDbm) &&
      (r.request.nodeId == 0 ? r.mode == PinMode::NotApplicable :
       validMode(r.mode) || (r.mode == PinMode::Safe && (r.flags & 2)));
}
class ISettingsStore {
 public:
  virtual ~ISettingsStore() = default;
  virtual bool read(std::uint8_t* data, std::size_t length) = 0;
  virtual bool write(const std::uint8_t* data, std::size_t length) = 0;
};
class SettingsService {
 public:
  SettingsService(ISettingsStore& store, std::uint32_t network, std::uint8_t node,
                  Profile profile, PinMode defaultMode, std::int8_t defaultPower)
      : store_(store), network_(network), node_(node), profile_(profile),
        defaults_{defaultMode, defaultPower}, selected_(defaults_) {}
  bool begin() {
    pending_ = false; invalid_ = false; defaultsValid_ = false; selected_ = defaults_;
    if (node_ > 8 || !validPower(profile_, defaults_.powerDbm) ||
        (node_ == 0 ? defaults_.mode != PinMode::NotApplicable : !validMode(defaults_.mode))) {
      rejectInvalidDefaults(); return false;
    }
    defaultsValid_ = true;
    std::array<std::uint8_t, kRecordSize> b{};
    // An unreadable/foreign/corrupt record is not an alternative operating
    // mode. Use this image's validated board/node defaults without writing
    // Flash; flag the rejected storage until an explicit setting is saved.
    if (!store_.read(b.data(), b.size())) { invalid_ = true; return true; }
    if (std::all_of(b.begin(), b.end(), [](std::uint8_t v) { return v == 255; })) return true;
    if (b[0] != 'F' || b[1] != 'L' || b[2] != 'S' || b[3] != 1 ||
        b[4] != static_cast<std::uint8_t>(profile_) || b[5] != node_ ||
        get32(b.data() + 8) != network_ || !crcValid(b.data(), b.size()) ||
        !validPower(profile_, static_cast<std::int8_t>(b[7])) ||
        (node_ == 0 ? b[6] != 255 : !validMode(static_cast<PinMode>(b[6])))) {
      invalid_ = true; return true;
    }
    for (unsigned i = 12; i < 22; ++i) if (b[i]) { invalid_ = true; return true; }
    selected_ = {static_cast<PinMode>(b[6]), static_cast<std::int8_t>(b[7])}; return true;
  }
  bool process(const Request& r, Reply& reply, std::uint32_t now, bool maintenanceBusy = false) {
    if (!validRequest(r) || r.networkId != network_ || r.nodeId != node_) return false;
    Status status = defaultsValid_ ? Status::Ok : Status::Unsupported;
    if (defaultsValid_ && r.operation != Operation::Query) {
      if (pending_) status = sameRequest(r, pendingRequest_) ? Status::Ok : Status::Busy;
      else if (maintenanceBusy) status = Status::Busy;
      else {
        Selection next = selected_;
        if (r.operation == Operation::SetPinMode) {
          if (node_ == 0) status = Status::Unsupported;
          else if (!validMode(static_cast<PinMode>(r.value))) status = Status::BadRequest;
          else next.mode = static_cast<PinMode>(r.value);
        } else {
          if (!validPower(profile_, static_cast<std::int8_t>(r.value))) status = Status::BadRequest;
          else next.powerDbm = static_cast<std::int8_t>(r.value);
        }
        // Both explicit setters can persist the validated fallback selection.
        if (status == Status::Ok && node_ != 0 && !validMode(next.mode)) status = Status::BadRequest;
        if (status == Status::Ok && (invalid_ || next.mode != selected_.mode || next.powerDbm != selected_.powerDbm)) {
          std::array<std::uint8_t, kRecordSize> b{};
          b[0] = 'F'; b[1] = 'L'; b[2] = 'S'; b[3] = 1; b[4] = static_cast<std::uint8_t>(profile_);
          b[5] = node_; b[6] = static_cast<std::uint8_t>(next.mode); b[7] = static_cast<std::uint8_t>(next.powerDbm);
          put32(b.data() + 8, network_); seal(b.data(), b.size());
          if (!store_.write(b.data(), b.size())) status = Status::StorageError;
          else { selected_ = next; invalid_ = false; pending_ = true; pendingRequest_ = r; pendingMs_ = now; }
        }
      }
    }
    reply = {r, status, profile_, selected_.mode, selected_.powerDbm,
      static_cast<std::uint8_t>((pending_ ? 1 : 0) | (invalid_ ? 2 : 0)),
      node_ == 0 ? 0x67U : 0x78U};
    return true;
  }
  Selection selected() const { return selected_; }
  bool pending() const { return pending_; }
  bool invalidRecord() const { return invalid_; }
  bool rebootDue(std::uint32_t now) const { return pending_ && std::uint32_t(now - pendingMs_) >= kApplyGraceMs; }
 private:
  // A broken build profile is distinct from rejected EEPROM. Never guess a
  // working GPIO role or allow a setting request to repair invalid firmware.
  void rejectInvalidDefaults() { invalid_ = true; selected_.mode = node_ == 0 ? PinMode::NotApplicable : PinMode::Safe;
    selected_.powerDbm = profile_ == Profile::Nano ? -10 : -18; }
  ISettingsStore& store_;
  const std::uint32_t network_;
  const std::uint8_t node_;
  const Profile profile_;
  const Selection defaults_;
  Selection selected_;
  bool invalid_{false}, pending_{false}, defaultsValid_{false};
  Request pendingRequest_{};
  std::uint32_t pendingMs_{0};
};
}  // namespace fanlink::settings
