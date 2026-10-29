#include <cassert>
#include <cstdio>
#include <deque>
#include "fanlink/tach_diagnostic.hpp"
#include "fanlink/rx_controller.hpp"
#include "fanlink/tx_controller.hpp"
using namespace fanlink;
namespace d = fanlink::tachdiag;
struct Clock : IClock {
  std::uint32_t us{0}; std::uint32_t millis() const override { return us / 1000; }
  std::uint32_t micros() const override { return us; }
  void delayMilliseconds(std::uint32_t n) override { us += n * 1000; }
  void delayMicroseconds(std::uint32_t n) override { us += n; }
};
struct Radio : IRadioPort {
  Clock& clock; bool fail{false}; unsigned sends{0}; std::deque<d::Packet> inbox;
  explicit Radio(Clock& c) : clock(c) {}
  bool begin(std::uint32_t) override { return true; }
  RadioResult transmit(const std::uint8_t*, std::size_t) override {
    ++sends; return fail ? RadioResult::IoError : RadioResult::Ok;
  }
  RadioRxResult receive(std::uint8_t* p, std::size_t, std::uint32_t ms) override {
    if (!inbox.empty()) { auto b=inbox.front(); inbox.pop_front();
      std::copy(b.begin(), b.end(), p); return {RadioResult::Ok, b.size()}; }
    clock.delayMilliseconds(ms); return {RadioResult::Timeout};
  }
  int lastError() const override { return 0; }
};
struct Tach : ITachometer {
  unsigned reads{0}; TachDiagnostics result{50, 20, 9, 1500, 3, true};
  bool begin() override { return true; }
  TachSample sample(std::uint32_t) override { return {1400, true}; }
  TachDiagnostics diagnostics(std::uint32_t) override { ++reads; return result; }
};
struct Fan : IFanOutput {
  std::uint8_t duty{255}; bool begin() override { return true; }
  void setDuty(std::uint8_t d) override { duty=d; }
  void setFullSpeed() override { duty=255; }
  std::uint8_t appliedDuty() const override { return duty; }
};
struct Maintenance : IRxMaintenance {
  bool busy{false}; unsigned calls{0};
  void tick(std::uint32_t) override {} bool active() const override { return busy; }
  bool handle(const std::uint8_t*, std::size_t, std::uint8_t*, std::size_t& n, std::uint32_t) override {
    ++calls; n=0; return false;
  }
};
int main() {
  const d::Request request{7, 123, 1}; d::Packet packet{};
  assert(d::encodeRequest(request, packet)); d::Request decoded{};
  assert(d::decodeRequest(packet.data(), packet.size(), decoded)); assert(d::same(request, decoded));
  for (unsigned i=0; i<30; ++i) { auto bad=packet; bad[i]^=1;
    assert(!d::decodeRequest(bad.data(),bad.size(),decoded)); }
  for (unsigned i=13; i<28; ++i) { auto bad=packet; bad[i]=1; d::seal(bad);
    assert(!d::decodeRequest(bad.data(),bad.size(),decoded)); }
  assert(!d::decodeRequest(packet.data(),29,decoded));
  for (auto r : {d::Request{7,0,1}, d::Request{7,1,0}, d::Request{7,1,9}})
    assert(!d::encodeRequest(r,packet));
  d::encodeRequest(request,packet);
  d::Reply response{request,true,UINT32_MAX,123,456,65534}; d::Packet reply{};
  assert(d::encodeReply(response,reply)); d::Reply parsed{};
  assert(d::decodeReply(reply.data(),30,parsed)); assert(parsed.rawEdges==UINT32_MAX);
  auto bad=reply; bad[13]=2; d::seal(bad); assert(!d::decodeReply(bad.data(),30,parsed));
  { Clock clock; Tach tach; Maintenance delegate; d::RxMaintenance rx(delegate,tach,clock,7,1);
    std::size_t n=0; assert(rx.handle(packet.data(),30,reply.data(),n,0));
    assert(n==30 && !rx.active() && !rx.replyRequiresFullSpeed() && tach.reads==1);
    assert(d::decodeReply(reply.data(),n,parsed)); assert(parsed.lastEdgeAgeMs==1);
    tach.result.lastEdgeAgeUs=UINT32_MAX; rx.handle(packet.data(),30,reply.data(),n,0);
    d::decodeReply(reply.data(),n,parsed); assert(parsed.lastEdgeAgeMs==65535);
    tach.result.lastEdgeAgeUs=100000000; rx.handle(packet.data(),30,reply.data(),n,0);
    d::decodeReply(reply.data(),n,parsed); assert(parsed.lastEdgeAgeMs==65534);
    tach.result={}; rx.handle(packet.data(),30,reply.data(),n,0); d::decodeReply(reply.data(),n,parsed);
    assert(!parsed.enabled && parsed.rawEdges==0 && parsed.lastEdgeAgeMs==65535);
    delegate.busy=true; rx.handle(packet.data(),30,reply.data(),n,0); assert(n==0 && rx.replyRequiresFullSpeed());
    delegate.busy=false; auto other=request; other.node=2; d::Packet p{}; d::encodeRequest(other,p);
    const auto reads=tach.reads; rx.handle(p.data(),30,reply.data(),n,0); assert(n==0 && reads==tach.reads);
    rx.handle(packet.data(),29,reply.data(),n,0); assert(delegate.calls==1 && rx.replyRequiresFullSpeed());
  }
  { Clock clock; Radio radio(clock); d::TxBridge tx(radio,clock,7);
    assert(!tx.enqueue(packet.data(),30,4,true)); assert(tx.enqueue(packet.data(),30,4,false));
    assert(!tx.enqueue(packet.data(),30,5,false)); assert(radio.sends==0); tx.startAfterSlot();
    for (unsigned field=0; field<3; ++field) { auto r=response;
      if(field==0) ++r.request.network; if(field==1) ++r.request.session; if(field==2) ++r.request.node;
      d::encodeReply(r,reply); radio.inbox.push_back(reply); tx.service(); assert(tx.waiting()); }
    d::encodeReply(response,reply); radio.inbox.push_back(reply); tx.service(); assert(!tx.active());
    std::uint16_t seq=0; assert(tx.replyReady(reply,seq) && seq==4); tx.replyWritten(false);
    assert(tx.replyReady(reply,seq)); tx.replyWritten(true); assert(!tx.replyReady(reply,seq));
    assert(!tx.enqueue(packet.data(),30,5,false)); clock.delayMilliseconds(1000);
    assert(tx.enqueue(packet.data(),30,5,false)); tx.startAfterSlot();
    for(unsigned i=0;i<21;++i) tx.service(); assert(!tx.active() && !tx.replyReady(reply,seq));
    clock.delayMilliseconds(1000); assert(tx.enqueue(packet.data(),30,6,false));
    clock.delayMilliseconds(250); tx.service(); assert(!tx.active());
    assert(tx.enqueue(packet.data(),30,7,false)); radio.fail=true; tx.startAfterSlot(); assert(!tx.active());
  }
  { Clock clock; Radio radio(clock); d::TxBridge tx(radio,clock,7);
    clock.us=UINT32_MAX-10000; assert(tx.enqueue(packet.data(),30,1,false)); tx.startAfterSlot();
    clock.delayMicroseconds(20001); tx.service(); assert(!tx.active());
  }
  // A diagnostic does not release duty or refresh the RX control watchdog.
  { Clock clock; Radio radio(clock); Tach tach; Fan fan; Maintenance delegate;
    d::RxMaintenance maintenance(delegate,tach,clock,7,1);
    RxController rx(radio,clock,fan,tach,RxControllerConfig{7,1}); rx.setMaintenance(&maintenance);
    assert(rx.begin()); ControlPacket control{}; control.networkId=7; control.sequence=1;
    control.pollId=2; control.duty.fill(90); d::Packet p{}; assert(encodeControlPacket(control,p));
    radio.inbox.push_back(p); rx.runOnce(); assert(fan.duty==90);
    clock.delayMilliseconds(450); radio.inbox.push_back(packet); rx.runOnce(); assert(fan.duty==90);
    clock.delayMilliseconds(51); rx.runOnce(); assert(fan.duty==255 && rx.linkFailsafeActive());
  }
  // Legacy maintenance implementations retain their default full-speed policy.
  { struct OldMaintenance : Maintenance {
      bool handle(const std::uint8_t*, std::size_t, std::uint8_t* p, std::size_t& n, std::uint32_t) override {
        std::fill_n(p,30,0); n=30; return true;
      }
    } maintenance;
    Clock clock; Radio radio(clock); Tach tach; Fan fan;
    RxController rx(radio,clock,fan,tach,RxControllerConfig{7,1}); assert(rx.begin());
    ControlPacket control{}; control.networkId=7; control.sequence=1; control.pollId=2; control.duty.fill(90);
    d::Packet p{}; encodeControlPacket(control,p); radio.inbox.push_back(p); rx.runOnce(); assert(fan.duty==90);
    rx.setMaintenance(&maintenance); radio.inbox.push_back(packet); rx.runOnce(); assert(fan.duty==255);
  }
  std::puts("PASS: tach wire, strict matching, arbitration, bounded timeout, read-only watchdog");
}
