#include <cassert>
#include <cstring>
#include <iostream>
#include "fanlink/settings_rx_maintenance.hpp"
#include "fanlink/platform/esp8266/settings_store.hpp"

using namespace fanlink;
using namespace fanlink::settings;
struct Store : ISettingsStore {
  std::array<std::uint8_t, kRecordSize> bytes{};
  unsigned writes{0}; bool readOk{true}, writeOk{true};
  const std::uint8_t* expectedFull{nullptr};
  Store() { bytes.fill(255); }
  bool read(std::uint8_t* b, std::size_t n) override { std::memcpy(b, bytes.data(), n); return readOk; }
  bool write(const std::uint8_t* b, std::size_t n) override {
    assert(!expectedFull || *expectedFull == 255);
    ++writes; if (!writeOk) return false; std::memcpy(bytes.data(), b, n); return true;
  }
};
struct Fan : IFanOutput {
  std::uint8_t duty{20}; unsigned fullCalls{0};
  bool begin() override { return true; }
  void setDuty(std::uint8_t d) override { duty = d; }
  void setFullSpeed() override { duty = 255; ++fullCalls; }
  std::uint8_t appliedDuty() const override { return duty; }
};
struct Update : ota::IUpdateStorage {
  bool begin(std::uint32_t, const std::array<std::uint8_t,16>&) override { return true; }
  bool write(const std::uint8_t*, std::size_t) override { return true; }
  bool verify() override { return true; }
  bool commit() override { return true; }
  void abort() override {}
};
static std::array<std::uint8_t, 4096> flash{};
static int readError = 0, eraseError = 0, writeError = 0;
static bool corruptWrite = false;
static void fallbackMatrix() {
  // TX plus all eight RX identities, both supported profiles and every pin
  // role. CRC-valid semantic faults must be rejected as well as bit damage.
  for (auto profile : {Profile::Lite, Profile::Nano}) {
    const std::int8_t power = profile == Profile::Lite ? 13 : 1;
    for (std::uint8_t node = 0; node <= 8; ++node) {
      for (auto mode : {PinMode::Uart, PinMode::Pwm, PinMode::PwmSwap}) {
        if (node == 0 && mode != PinMode::Uart) continue;
        const auto compiled = node == 0 ? PinMode::NotApplicable : mode;
        Store original; Reply reply{};
        SettingsService initial(original,1234,node,profile,compiled,power);
        assert(initial.begin() && !initial.invalidRecord());
        assert(original.writes == 0 && initial.selected().mode == compiled);
        // Save an override that differs from the default, then change the
        // compiled mode to prove a valid record wins without any rewriting.
        const std::int8_t savedPower = profile == Profile::Lite ? 0 : -6;
        assert(initial.process({1234,1,node,Operation::SetPower,static_cast<std::uint8_t>(savedPower)},reply,0));
        const auto changedDefault = node == 0 ? compiled :
            (mode == PinMode::Uart ? PinMode::PwmSwap : PinMode::Uart);
        SettingsService saved(original,1234,node,profile,changedDefault,power);
        assert(saved.begin() && !saved.invalidRecord() && !saved.pending());
        assert(saved.selected().mode == compiled && saved.selected().powerDbm == savedPower);
        assert(original.writes == 1);
        for (int fault = -1; fault <= 33; ++fault) {
          Store bad = original;
          if (fault == -1) bad.readOk = false;
          else if (fault < 24) bad.bytes[fault] ^= 1;
          else {
            const unsigned offsets[] = {0,3,4,5,6,7,8,12,16,21};
            const auto offset = offsets[fault - 24];
            bad.bytes[offset] = (offset == 6 || offset == 7) ? 127 : bad.bytes[offset] ^ 1;
            seal(bad.bytes.data(),bad.bytes.size());
          }
          const auto damaged = bad.bytes;
          SettingsService fallback(bad,1234,node,profile,compiled,power);
          assert(fallback.begin() && fallback.invalidRecord() && !fallback.pending());
          assert(fallback.selected().mode == compiled && fallback.selected().powerDbm == power);
          assert(bad.writes == 1 && bad.bytes == damaged);
          assert(fallback.process({1234,2,node,Operation::Query,0},reply,0));
          assert(reply.status == Status::Ok && reply.flags == 2 && reply.mode == compiled);
          std::array<std::uint8_t,kPacketSize> packet{}; Reply decoded{};
          assert(encodeReply(reply,packet) && decodeReply(packet.data(),packet.size(),decoded));
          assert(decoded.flags == 2 && decoded.powerDbm == power && bad.writes == 1);
          // Query/boot never hide the fault or write. A failed explicit save
          // preserves the working defaults, diagnostic and damaged record.
          assert(fallback.begin() && fallback.invalidRecord() && bad.writes == 1);
          bad.writeOk = false;
          const Request repair{1234,3,node,Operation::SetPower,static_cast<std::uint8_t>(power)};
          assert(fallback.process(repair,reply,10) && reply.status == Status::StorageError);
          assert(reply.flags == 2 && !fallback.pending() && bad.bytes == damaged);
          assert(fallback.selected().mode == compiled && fallback.selected().powerDbm == power);
          bad.writeOk = true;
          assert(fallback.process(repair,reply,20) && reply.status == Status::Ok);
          assert(reply.flags == 1 && !fallback.invalidRecord() && fallback.pending());
          assert(bad.writes == 3 && !fallback.rebootDue(2519) && fallback.rebootDue(2520));
          assert(fallback.process(repair,reply,100) && bad.writes == 3);
          bad.readOk = true;
          assert(fallback.begin() && !fallback.invalidRecord() && !fallback.pending());
          assert(fallback.selected().mode == compiled && fallback.selected().powerDbm == power);
        }
      }
    }
  }
  // Invalid compiled defaults remain a separate non-operational build error;
  // neither valid stored values nor a remote setting may bypass that guard.
  for (unsigned fault = 0; fault < 6; ++fault) {
    Store store;
    SettingsService bad(store,1234,fault == 0 ? 9 : fault == 1 ? 0 : 1,
        fault == 2 ? static_cast<Profile>(99) : Profile::Lite,
        fault == 3 ? PinMode::Safe : fault == 4 ? PinMode::NotApplicable : PinMode::Pwm,
        fault == 5 ? 14 : 13);
    assert(!bad.begin() && bad.invalidRecord() && !bad.pending());
    if (fault != 0) {
      Reply reply{};
      for (auto op : {Operation::Query,Operation::SetPinMode,Operation::SetPower}) {
        assert(bad.process({1234,1,static_cast<std::uint8_t>(fault == 1 ? 0 : 1),op,0},reply,0));
        assert(reply.status == Status::Unsupported && reply.flags == 2);
      }
    }
    assert(store.writes == 0);
  }
}
extern "C" std::int32_t flash_hal_read(std::uint32_t addr, std::uint32_t n, std::uint8_t* b) {
  assert(addr == 0xFB000 && n <= flash.size());
  if (!readError) std::memcpy(b, flash.data(), n); return readError;
}
extern "C" std::int32_t flash_hal_erase(std::uint32_t addr, std::uint32_t n) {
  assert(addr == 0xFB000 && n == flash.size());
  if (!eraseError) flash.fill(255); return eraseError;
}
extern "C" std::int32_t flash_hal_write(std::uint32_t addr, std::uint32_t n, const std::uint8_t* b) {
  assert(addr == 0xFB000 && n <= flash.size());
  if (!writeError) { std::memcpy(flash.data(), b, n); if (corruptWrite) flash[0] ^= 1; }
  return writeError;
}
int main() {
  fallbackMatrix();
  // Both deployed boards are Lite: max defaults, saved overrides and strict
  // cross-profile isolation must all survive the Nano-to-Lite correction.
  for (std::uint8_t node : {0, 2}) {
    Store s;
    const auto mode = node == 0 ? PinMode::NotApplicable : PinMode::Uart;
    SettingsService lite(s,1234,node,Profile::Lite,mode,13);
    assert(lite.begin() && lite.selected().powerDbm == 13 && s.writes == 0);
    Reply r{};
    assert(lite.process({1234,1,node,Operation::SetPower,0},r,0));
    assert(r.status == Status::Ok);
    SettingsService restart(s,1234,node,Profile::Lite,mode,13);
    assert(restart.begin() && restart.selected().powerDbm == 0 && s.writes == 1);
    s.bytes[0] ^= 1;
    assert(restart.begin() && restart.invalidRecord() && restart.selected().powerDbm == 13);
    assert(restart.selected().mode == mode && s.writes == 1);
  }
  {
    Store s; Reply r{};
    SettingsService oldNano(s,1234,2,Profile::Nano,PinMode::Uart,-6);
    assert(oldNano.begin());
    assert(oldNano.process({1234,1,2,Operation::SetPower,static_cast<std::uint8_t>(-3)},r,0));
    SettingsService lite(s,1234,2,Profile::Lite,PinMode::Uart,13);
    assert(lite.begin() && lite.invalidRecord() && lite.selected().powerDbm == 13);
    assert(lite.process({1234,2,2,Operation::SetPower,13},r,0) && r.status == Status::Ok);
    assert(lite.begin() && !lite.invalidRecord() && lite.selected().mode == PinMode::Uart);
    assert(lite.process({1234,3,2,Operation::SetPinMode,0},r,0) && r.status == Status::Ok);
    assert(lite.begin() && lite.selected().mode == PinMode::Uart);
    assert(lite.process({1234,4,2,Operation::SetPower,13},r,0) && r.status == Status::Ok);
    assert(lite.begin() && lite.selected().powerDbm == 13 && !lite.invalidRecord());
  }
  Request req{1234, 4, 2, Operation::SetPinMode, 0};
  std::array<std::uint8_t, kPacketSize> wire{};
  assert(encodeRequest(req, wire)); Request decoded{};
  assert(decodeRequest(wire.data(), wire.size(), decoded) && sameRequest(req, decoded));
  for (std::size_t i = 0; i < wire.size(); ++i) {
    auto bad = wire; bad[i] ^= 1; assert(!decodeRequest(bad.data(), bad.size(), decoded));
  }
  for (unsigned i = 15; i < 28; ++i) {
    auto bad = wire; bad[i] = 1; seal(bad.data(), bad.size());
    assert(!decodeRequest(bad.data(), bad.size(), decoded));
  }
  assert(!decodeRequest(nullptr, 30, decoded));
  for (unsigned n = 0; n < 35; ++n) if (n != 30) assert(!decodeRequest(wire.data(), n, decoded));
  assert(!validRequest({1,0,1,Operation::Query,0}));
  assert(!validRequest({1,1,9,Operation::Query,0}));
  assert(!validRequest({1,1,1,Operation::Query,1}));
  for (int power = -128; power <= 127; ++power) {
    assert(validPower(Profile::Lite, power) == (power >= -18 && power <= 13));
    assert(validPower(Profile::Nano, power) == (power == -10 || power == -6 || power == -3 || power == 1));
  }
  Store store;
  SettingsService service(store,1234,2,Profile::Nano,PinMode::Pwm,-6);
  assert(service.begin() && !service.invalidRecord() && store.writes == 0);
  Reply reply{};
  assert(service.process({1234,1,2,Operation::Query,0},reply,0));
  assert(reply.capabilities == 0x78 && reply.mode == PinMode::Pwm && reply.flags == 0);
  assert(encodeReply(reply,wire)); Reply parsed{};
  assert(decodeReply(wire.data(),wire.size(),parsed) && parsed.powerDbm == -6);
  auto wrong = req; wrong.nodeId = 1; assert(!service.process(wrong,reply,0));
  wrong = req; wrong.networkId++; assert(!service.process(wrong,reply,0));
  assert(store.writes == 0);
  assert(service.process(req,reply,0,true) && reply.status == Status::Busy && store.writes == 0);
  store.writeOk = false;
  assert(service.process(req,reply,0) && reply.status == Status::StorageError);
  assert(!service.pending() && service.selected().mode == PinMode::Pwm);
  store.writeOk = true;
  assert(service.process(req,reply,UINT32_MAX - 100) && reply.status == Status::Ok);
  assert(service.pending() && reply.flags == 1 && service.selected().mode == PinMode::Uart);
  const auto writes = store.writes;
  assert(service.process(req,reply,2000) && reply.status == Status::Ok && store.writes == writes);
  assert(!service.rebootDue(2398) && service.rebootDue(2399));
  auto conflict = req; conflict.session++;
  assert(service.process(conflict,reply,2000) && reply.status == Status::Busy);
  assert(service.process({1234,50,2,Operation::Query,0},reply,2001) && reply.flags == 1);
  SettingsService reboot(store,1234,2,Profile::Nano,PinMode::Pwm,-6);
  assert(reboot.begin() && reboot.selected().mode == PinMode::Uart && !reboot.pending());
  assert(reboot.process(req,reply,0) && !reboot.pending() && store.writes == writes);
  req = {1234,5,2,Operation::SetPower,static_cast<std::uint8_t>(-3)};
  assert(reboot.process(req,reply,100) && reboot.selected().powerDbm == -3 && reboot.pending());
  assert(reboot.begin() && reboot.selected().powerDbm == -3);
  for (unsigned i = 0; i < kRecordSize; ++i) {
    Store bad = store; bad.bytes[i] ^= 1;
    SettingsService corrupt(bad,1234,2,Profile::Nano,PinMode::Pwm,-6);
    assert(corrupt.begin() && corrupt.invalidRecord() && corrupt.selected().mode == PinMode::Pwm);
    assert(corrupt.selected().powerDbm == -6);
    assert(corrupt.process({1234,1,2,Operation::Query,0},reply,0));
    assert(encodeReply(reply,wire) && decodeReply(wire.data(),wire.size(),parsed) && parsed.flags == 2);
    assert(corrupt.process(req,reply,0) && reply.status == Status::Ok);
    assert(corrupt.begin() && !corrupt.invalidRecord() && corrupt.selected().mode == PinMode::Pwm);
    assert(corrupt.process({1234,6,2,Operation::SetPinMode,2},reply,10) && reply.status == Status::Ok);
    assert(corrupt.pending() && !corrupt.invalidRecord() && corrupt.selected().mode == PinMode::PwmSwap);
  }
  Store unread; unread.readOk = false;
  SettingsService failed(unread,1234,2,Profile::Nano,PinMode::Pwm,-6);
  assert(failed.begin() && failed.invalidRecord() && failed.selected().mode == PinMode::Pwm);
  for (auto byte : {3,4,5,8,12}) {
    Store foreign = store; foreign.bytes[byte] ^= 1; seal(foreign.bytes.data(),foreign.bytes.size());
    SettingsService isolated(foreign,1234,2,Profile::Nano,PinMode::Pwm,-6);
    assert(isolated.begin() && isolated.invalidRecord() && isolated.selected().mode == PinMode::Pwm);
  }
  Store txStore;
  SettingsService tx(txStore,1234,0,Profile::Lite,PinMode::NotApplicable,13);
  assert(tx.begin());
  assert(tx.process({1234,1,0,Operation::SetPinMode,0},reply,0) && reply.status == Status::Unsupported);
  assert(tx.process({1234,1,0,Operation::SetPower,14},reply,0) && reply.status == Status::BadRequest);
  assert(tx.process({1234,1,0,Operation::SetPower,static_cast<std::uint8_t>(-18)},reply,0));
  assert(reply.status == Status::Ok && reply.capabilities == 0x67 && tx.pending());
  Fan fan; Update update;
  Store maintenanceStore;
  maintenanceStore.expectedFull = &fan.duty;
  SettingsService settings(maintenanceStore,1234,2,Profile::Nano,PinMode::Pwm,-6);
  assert(settings.begin());
  ota::Receiver receiver(update,fan,1234,2,77);
  settings::RxMaintenance maintenance(settings,receiver,fan);
  req = {1234,20,2,Operation::SetPinMode,0}; assert(encodeRequest(req,wire));
  std::array<std::uint8_t,30> response{}; std::size_t size = 0;
  assert(maintenance.handle(wire.data(),wire.size(),response.data(),size,0));
  assert(size == 30 && fan.duty == 255 && maintenance.active());
  fan.duty = 10; maintenance.tick(10); assert(fan.duty == 255);
  assert(maintenance.handle(wire.data(),wire.size(),response.data(),size,2000));
  assert(settings.rebootDue(2500) && maintenanceStore.writes == 1);
  ota::Packet begin{1234,44,2,ota::Op::Begin,1024,{}};
  put32(begin.data.data(),77);
  assert(ota::encodeRequest(begin,wire));
  assert(maintenance.handle(wire.data(),wire.size(),response.data(),size,2100));
  assert(size == 0 && !receiver.active() && settings.pending());
  // After simulated reboot an OTA session blocks config writes but not Query.
  assert(settings.begin());
  assert(maintenance.handle(wire.data(),wire.size(),response.data(),size,2200));
  assert(size == 30 && receiver.active());
  req.session++; req.value = 2; assert(encodeRequest(req,wire));
  assert(maintenance.handle(wire.data(),wire.size(),response.data(),size,2201));
  assert(decodeReply(response.data(),size,parsed) && parsed.status == Status::Busy);
  assert(!settings.pending() && maintenanceStore.writes == 1);
  req.operation = Operation::Query; req.value = 0; assert(encodeRequest(req,wire));
  assert(maintenance.handle(wire.data(),wire.size(),response.data(),size,2202));
  assert(decodeReply(response.data(),size,parsed) && parsed.status == Status::Ok && receiver.active());
  platform::esp8266::Esp8266SettingsStore hardware;
  std::array<std::uint8_t,kRecordSize> record{}; record.fill(33);
  assert(hardware.write(record.data(),record.size()));
  readError = -1; assert(!hardware.read(record.data(),record.size())); readError = 0;
  eraseError = -1; assert(!hardware.write(record.data(),record.size())); eraseError = 0;
  writeError = -1; assert(!hardware.write(record.data(),record.size())); writeError = 0;
  corruptWrite = true; assert(!hardware.write(record.data(),record.size()));
  std::cout << "PASS: settings wire, persistence, retry, corruption, power, RX arbitration and Flash failures\n";
}
