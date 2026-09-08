#include <cassert>
#include <cstdio>
#include <deque>
#include "fanlink/settings_tx.hpp"
using namespace fanlink;
namespace s = fanlink::settings;
struct Clock : IClock {
  std::uint32_t us{0};
  std::uint32_t millis() const override { return us / 1000; }
  std::uint32_t micros() const override { return us; }
  void delayMilliseconds(std::uint32_t n) override { us += n * 1000; }
  void delayMicroseconds(std::uint32_t n) override { us += n; }
};
struct Store : s::ISettingsStore {
  std::array<std::uint8_t, s::kRecordSize> bytes{}; int writes{0}; bool failWrite{false};
  Store() { bytes.fill(255); }
  bool read(std::uint8_t* p, std::size_t n) override { std::copy_n(bytes.begin(), n, p); return true; }
  bool write(const std::uint8_t* p, std::size_t n) override {
    ++writes; if (failWrite) return false;
    std::copy_n(p, n, bytes.begin()); return true;
  }
};
struct Radio : IRadioPort {
  Clock& clock; unsigned controls{0}, configs{0}; bool fail{false};
  std::deque<std::array<std::uint8_t, 30>> replies;
  explicit Radio(Clock& c) : clock(c) {}
  bool begin(std::uint32_t) override { return true; }
  RadioResult transmit(const std::uint8_t* p, std::size_t n) override {
    s::Request r{};
    if (s::decodeRequest(p,n,r)) { ++configs; return fail ? RadioResult::IoError : RadioResult::Ok; }
    ControlPacket control{}; assert(decodeControlPacket(p,n,control));
    for (auto duty : control.duty) assert(duty == 255);
    ++controls; return RadioResult::Ok;
  }
  RadioRxResult receive(std::uint8_t* p, std::size_t, std::uint32_t ms) override {
    if (!replies.empty()) { const auto b=replies.front(); replies.pop_front();
      std::copy(b.begin(), b.end(), p); return {RadioResult::Ok, b.size()}; }
    clock.delayMilliseconds(ms); return {RadioResult::Timeout};
  }
  int lastError() const override { return 0; }
};
struct Test {
  Clock clock; Radio radio{clock}; Store store;
  s::SettingsService local{store, 7, 0, s::Profile::Lite, s::PinMode::NotApplicable, 13};
  TxController tx{radio,clock,TxControllerConfig{7}};
  s::TxSettings bridge{radio,clock,tx,local,7};
  Test() { assert(local.begin()); assert(tx.begin()); }
  bool enqueue(s::Request r, bool busy=false, unsigned sequence=12) {
    std::array<std::uint8_t,30> b{}; if(!s::encodeRequest(r,b)) return false;
    return bridge.enqueue(b.data(),b.size(),sequence,busy);
  }
  s::Reply reply(unsigned sequence=12) {
    std::array<std::uint8_t,30> b{}; std::uint16_t seq=0;
    assert(bridge.replyReady(b,seq)); assert(seq==sequence); s::Reply r{};
    assert(s::decodeReply(b.data(),b.size(),r)); return r;
  }
  void receive(s::Request request, unsigned flags=0) {
    s::Reply r{request,s::Status::Ok,s::Profile::Nano,s::PinMode::PwmSwap,-6,
      static_cast<std::uint8_t>(flags),0x78};
    std::array<std::uint8_t,30> b{}; assert(s::encodeReply(r,b)); radio.replies.push_back(b);
  }
  bool haveReply() { std::array<std::uint8_t,30>b{}; std::uint16_t seq=0; return bridge.replyReady(b,seq); }
};
int main() {
  const s::Request query{7,9,0,s::Operation::Query,0};
  { Test t; assert(t.enqueue(query)); assert(t.bridge.service());
    assert(t.reply().capabilities==0x67); assert(t.store.writes==0); assert(t.radio.controls==0);
    t.bridge.replyWritten(false); assert(t.haveReply()); t.bridge.replyWritten(true);
    t.bridge.service(); assert(!t.bridge.active()); }
  { Test t; auto r=query; r.operation=s::Operation::SetPower; r.value=10;
    t.tx.setNodeDuty(1,0); assert(t.enqueue(r));
    while(t.clock.millis()<600) { t.bridge.service(); t.clock.delayMilliseconds(1); }
    assert(t.store.writes==0); assert(t.radio.controls==5);
    t.bridge.service(); assert(t.reply().flags==1); assert(t.store.writes==1);
    t.bridge.replyWritten(true); const auto start=t.clock.millis();
    assert(t.enqueue(r,false,13)); assert(t.reply(13).flags==1); assert(t.store.writes==1);
    t.bridge.replyWritten(true); r.value=11; assert(!t.enqueue(r));
    t.clock.delayMilliseconds(2499); assert(!t.local.rebootDue(t.clock.millis()));
    assert(!t.bridge.localRebootReady());
    t.clock.delayMilliseconds(1); assert(t.local.rebootDue(start+2500));
    assert(t.bridge.localRebootReady()); }
  { Test t; auto r=query; r.operation=s::Operation::SetPower; r.value=10;
    assert(t.enqueue(r)); t.clock.delayMilliseconds(600); t.bridge.service();
    assert(t.store.writes==1 && t.local.pending());
    assert(!t.bridge.localRebootReady());
    // UART is full for longer than the apply grace. Persisted data is not
    // permission to restart before the ACK has actually entered the UART.
    t.bridge.replyWritten(false); t.clock.delayMilliseconds(5000);
    t.bridge.service(); assert(t.haveReply() && t.bridge.active());
    assert(t.local.rebootDue(t.clock.millis())); assert(!t.bridge.localRebootReady());
    assert(t.tx.requestedDuty()[0]==255);
    assert(!t.enqueue(r,false,13)); // Existing unsent ACK is retained verbatim.
    assert(t.reply().request.value==10);
    t.bridge.replyWritten(true); assert(t.bridge.localRebootReady());
    assert(t.store.writes==1); }
  { Test t; auto r=query; r.operation=s::Operation::SetPower; r.value=10;
    t.store.failWrite=true; assert(t.enqueue(r)); t.clock.delayMilliseconds(600);
    t.bridge.service(); assert(t.reply().status==s::Status::StorageError);
    assert(!t.local.pending() && t.local.selected().powerDbm==13);
    t.bridge.replyWritten(true); t.clock.delayMilliseconds(10000); t.bridge.service();
    assert(!t.bridge.active() && !t.bridge.localRebootReady()); }
  { Test t; auto r=query; r.operation=s::Operation::SetPower; r.value=10;
    assert(t.enqueue(r,true)); assert(t.reply().status==s::Status::Busy);
    assert(t.store.writes==0); assert(!t.bridge.active()); }
  { Test t; auto r=query; r.nodeId=8; assert(t.enqueue(r));
    auto stale=r; ++stale.session; t.receive(stale); t.bridge.service(); assert(!t.haveReply());
    stale=r; stale.nodeId=7; t.receive(stale); t.bridge.service(); assert(!t.haveReply());
    stale=r; stale.operation=s::Operation::SetPower; stale.value=250;
    t.receive(stale); t.bridge.service(); assert(!t.haveReply());
    stale=r; ++stale.networkId; t.receive(stale); t.bridge.service(); assert(!t.haveReply());
    t.receive(r); t.radio.replies.back()[28]^=1U; t.bridge.service(); assert(!t.haveReply());
    t.receive(r); t.bridge.service(); assert(t.reply().request.nodeId==8);
    assert(t.radio.configs==1); t.bridge.replyWritten(true); t.bridge.service(); assert(!t.bridge.active()); }
  { Test t; auto r=query; r.nodeId=1; assert(t.enqueue(r));
    for(unsigned i=0;i<400 && t.bridge.active();++i) { t.bridge.service(); t.clock.delayMilliseconds(1); }
    assert(!t.bridge.active()); assert(!t.haveReply()); }
  { Test t; auto r=query; r.nodeId=1; t.radio.fail=true;
    assert(t.enqueue(r)); t.bridge.service();
    assert(t.radio.configs==1 && !t.bridge.active() && !t.haveReply()); }
  { Test t; auto r=query; r.nodeId=2; r.operation=s::Operation::SetPower; r.value=250;
    assert(t.enqueue(r)); t.clock.delayMilliseconds(600); t.bridge.service();
    auto changed=r; changed.value=246; t.receive(changed); t.bridge.service();
    assert(!t.haveReply());
    t.clock.delayMilliseconds(300); t.bridge.service();
    assert(!t.bridge.active() && !t.haveReply());
    assert(t.tx.requestedDuty()[0]==255 && t.store.writes==0);
    assert(!t.bridge.localRebootReady()); }
  { Test t; auto r=query; r.nodeId=1; r.operation=s::Operation::SetPinMode; r.value=0;
    assert(t.enqueue(r)); t.clock.delayMilliseconds(600); t.receive(r,1); t.bridge.service();
    assert(t.reply().flags==1); t.bridge.replyWritten(true);
    t.clock.delayMilliseconds(2499); t.bridge.service(); assert(t.bridge.active());
    t.clock.delayMilliseconds(2); t.bridge.service(); assert(!t.bridge.active()); }
  { Test t; auto r=query; r.networkId=8; assert(!t.enqueue(r)); r=query; r.nodeId=9; assert(!t.enqueue(r)); }
  std::puts("PASS: TX settings query, prelude, idempotency, persistence, busy, strict RF ACK, timeout and grace");
}
