#include <cassert>
#include <cstdio>
#include <deque>
#include <vector>
#include "fanlink/ota_tx_bridge.hpp"
#include "fanlink/ota_rx_maintenance.hpp"
#include "fanlink/rx_controller.hpp"
#include "fanlink/byte_utils.hpp"

using namespace fanlink;
namespace {
struct Clock : IClock {
  std::uint64_t us{0};
  std::uint32_t millis() const override { return us / 1000; }
  std::uint32_t micros() const override { return us; }
  void delayMilliseconds(std::uint32_t n) override { us += n * 1000; }
  void delayMicroseconds(std::uint32_t n) override { us += n; }
};
struct Fan : IFanOutput {
  std::uint8_t duty{255};
  bool begin() override { return true; }
  void setDuty(std::uint8_t n) override { duty=n; }
  void setFullSpeed() override { duty=255; }
  std::uint8_t appliedDuty() const override { return duty; }
};
struct Tach : ITachometer {
  bool begin() override { return true; }
  TachSample sample(std::uint32_t) override { return {1000,true}; }
};
struct Store : ota::IUpdateStorage {
  Fan& fan; unsigned writes{0}, commits{0}, aborts{0};
  explicit Store(Fan& f):fan(f){}
  bool begin(std::uint32_t,const std::array<std::uint8_t,16>&) override {
    assert(fan.duty==255); return true;
  }
  bool write(const std::uint8_t*,std::size_t) override {
    assert(fan.duty==255); ++writes; return true;
  }
  bool verify() override { assert(fan.duty==255); return true; }
  bool commit() override { assert(fan.duty==255); ++commits; return true; }
  void abort() override { ++aborts; }
};
struct Radio : IRadioPort {
  Clock& clock; ota::Receiver* peer{nullptr}; bool dropAck{false};
  unsigned controls{0}, requests{0}, replies{0}, begins{0};
  std::deque<std::array<std::uint8_t,30>> incoming;
  explicit Radio(Clock& c):clock(c){}
  bool begin(std::uint32_t) override { ++begins; return true; }
  int lastError() const override { return 0; }
  RadioResult transmit(const std::uint8_t* data,std::size_t len) override {
    ota::Packet p{},reply{}; ControlPacket control{};
    if (ota::decodeRequest(data,len,p)) {
      ++requests;
      if (peer && peer->process(p,reply,clock.millis())) {
        std::array<std::uint8_t,30> encoded{};
        assert(ota::encodeReply(reply,encoded));
        if (!dropAck) incoming.push_back(encoded);
        dropAck=false;
      }
    } else if (decodeControlPacket(data,len,control)) {
      ++controls;
      for(auto d:control.duty) assert(d==255);
    } else ++replies;
    return RadioResult::Ok;
  }
  RadioRxResult receive(std::uint8_t* out,std::size_t,std::uint32_t wait) override {
    if(incoming.empty()) { clock.delayMilliseconds(wait); return {RadioResult::Timeout}; }
    const auto data=incoming.front(); incoming.pop_front();
    std::copy(data.begin(),data.end(),out); return {RadioResult::Ok,30,-50,0};
  }
};
ota::Packet command(ota::Op op,std::uint32_t offset=0) {
  ota::Packet p{}; p.networkId=7; p.session=9; p.nodeId=1;
  p.operation=op; p.offset=offset; return p;
}
void queue(Radio& radio,const ota::Packet& p) {
  std::array<std::uint8_t,30> bytes{}; assert(ota::encodeRequest(p,bytes));
  radio.incoming.push_back(bytes);
}
void control(Radio& radio,std::uint16_t seq,std::uint8_t duty) {
  ControlPacket p{}; p.networkId=7; p.sequence=seq; p.pollId=2; p.duty.fill(duty);
  std::array<std::uint8_t,30> bytes{}; assert(encodeControlPacket(p,bytes));
  radio.incoming.push_back(bytes);
}
void txTest() {
  Clock clock; Fan fan; Store store(fan); ota::Receiver peer(store,fan,7,1,42);
  Radio radio(clock); radio.peer=&peer;
  TxController tx(radio,clock,TxControllerConfig{7}); assert(tx.begin());
  ota::TxBridge bridge(radio,clock,tx,7);
  auto begin=command(ota::Op::Begin,128); bytes::putU32Le(begin.data.data(),42);
  std::array<std::uint8_t,30> encoded{}, response{};
  assert(ota::encodeRequest(begin,encoded));
  assert(bridge.enqueue(encoded.data(),encoded.size(),123));
  std::uint16_t seq=0;
  while(clock.millis()<600) { assert(bridge.service()); assert(radio.requests==0); clock.delayMilliseconds(1); }
  assert(radio.controls>=4); assert(bridge.service());
  assert(bridge.takeReply(response,seq) && seq==123);
  unsigned sequence=124;
  auto send=[&](const ota::Packet& p,bool lost=false) {
    assert(ota::encodeRequest(p,encoded)); radio.dropAck=lost;
    clock.delayMicroseconds(20000U);
    assert(bridge.enqueue(encoded.data(),30,sequence)); bridge.service();
    if(lost) {
      assert(!bridge.takeReply(response,seq));
      assert(bridge.enqueue(encoded.data(),30,sequence)); bridge.service();
    }
    assert(bridge.takeReply(response,seq)); assert(seq==sequence++);
    ota::Packet reply{}; assert(ota::decodeReply(response.data(),30,reply));
    assert(reply.data[0]==0);
  };
  send(command(ota::Op::Hash,0)); send(command(ota::Op::Hash,10));
  for(unsigned offset=0;offset<128;offset+=10) {
    auto p=command(ota::Op::Data,offset); p.data.fill(0);
    send(p,offset==40);
  }
  assert(store.writes==13);
  send(command(ota::Op::Finish,128)); assert(store.commits==0);
  send(command(ota::Op::Commit,128),true); assert(store.commits==1);
  clock.delayMilliseconds(10001); bridge.service(); assert(!bridge.active());
  for(auto d:tx.requestedDuty()) assert(d==255);
}
void fastTxTest() {
  Clock clock; Fan fan; Store store(fan); ota::Receiver peer(store,fan,7,1,42);
  Radio radio(clock); radio.peer=&peer;
  TxController tx(radio,clock,TxControllerConfig{7}); assert(tx.begin());
  ota::TxBridge bridge(radio,clock,tx,7);
  std::array<std::uint8_t,30> encoded{}, response{}; std::uint16_t seq=0;
  auto send=[&](ota::Packet p) {
    assert(ota::encodeRequest(p,encoded)); assert(bridge.enqueue(encoded.data(),30,42));
    while(clock.millis()<600) { bridge.service(); clock.delayMilliseconds(1); }
    bridge.service(); assert(bridge.takeReply(response,seq));
  };
  auto begin=command(ota::Op::Begin,128); bytes::putU32Le(begin.data.data(),42);
  send(begin); send(command(ota::Op::Hash)); send(command(ota::Op::Hash,10));
  for (unsigned base : {0U,100U}) {
    auto fragment=command(ota::Op::Fragment,base);
    assert(ota::encodeRequest(fragment,encoded));
    const unsigned count=std::min(100U,128U-base);
    std::vector<std::uint8_t> payload(encoded.begin(),encoded.end());
    payload.resize(30+count-10,0);
    assert(!bridge.enqueueBlock(payload.data(),payload.size()-1,99));
    assert(!bridge.enqueue(encoded.data(),30,99));
    radio.dropAck=true;
    clock.delayMicroseconds(20000U);
    assert(bridge.enqueueBlock(payload.data(),payload.size(),99));
    const auto start=clock.us; bridge.service();
    assert(clock.us-start>=((count+9)/10)*1000U);
    assert(!bridge.takeReply(response,seq));
    assert(store.writes==base/100+1);
    assert(bridge.enqueueBlock(payload.data(),payload.size(),99));
    clock.delayMicroseconds(20000U); bridge.service();
    assert(bridge.takeReply(response,seq) && seq==99);
    ota::Packet ack{}; assert(ota::decodeReply(response.data(),30,ack));
    assert(ack.operation==ota::Op::BlockFinish && ack.offset==base+count && ack.data[0]==0);
    assert(store.writes==base/100+1);
  }
  send(command(ota::Op::Finish,128)); send(command(ota::Op::Commit,128));
  assert(store.commits==1 && fan.duty==255);
}
void imageRateTest(bool rollover) {
  Clock clock; Fan fan; Store store(fan); ota::Receiver peer(store,fan,7,1,42);
  if(rollover) clock.us=(1ULL<<32)-615000U;
  Radio radio(clock); radio.peer=&peer;
  TxController tx(radio,clock,TxControllerConfig{7}); assert(tx.begin());
  ota::TxBridge bridge(radio,clock,tx,7);
  std::array<std::uint8_t,30> encoded{}, response{}; std::uint16_t seq=0;
  auto send=[&](ota::Packet p) {
    assert(ota::encodeRequest(p,encoded)); assert(bridge.enqueue(encoded.data(),30,42));
    bool received=false;
    for(unsigned i=0; i<1000 && !received; ++i) {
      bridge.service(); received=bridge.takeReply(response,seq);
      if(!received) clock.delayMilliseconds(1);
    }
    assert(received);
  };
  auto begin=command(ota::Op::Begin,200); bytes::putU32Le(begin.data.data(),42);
  send(begin); send(command(ota::Op::Hash)); send(command(ota::Op::Hash,10));
  auto fragment=command(ota::Op::Fragment,0);
  assert(ota::encodeRequest(fragment,encoded));
  std::vector<std::uint8_t> payload(encoded.begin(),encoded.end()); payload.resize(120,0);
  assert(bridge.enqueueBlock(payload.data(),payload.size(),42));
  const auto firstStart=clock.us;
  bridge.service(); assert(bridge.takeReply(response,seq));
  // A host that ignores pacing cannot force another full/retransmitted batch.
  assert(bridge.enqueueBlock(payload.data(),payload.size(),42));
  const auto requests=radio.requests;
  bridge.service(); assert(radio.requests==requests);
  constexpr auto interval=(100U*1000000U+ota::kImageBytesPerSecond-1)/ota::kImageBytesPerSecond;
  clock.us=firstStart+interval-1;
  bridge.service(); assert(radio.requests==requests);
  ++clock.us; bridge.service(); assert(radio.requests==requests+11);
  assert(bridge.takeReply(response,seq)); assert(store.writes==1);
  // Idle time supplies no accumulated credit: a new send restarts its budget.
  clock.delayMilliseconds(1000);
  assert(bridge.enqueueBlock(payload.data(),payload.size(),42)); bridge.service();
  assert(bridge.takeReply(response,seq));
  assert(bridge.enqueueBlock(payload.data(),payload.size(),42));
  const auto afterIdle=radio.requests; bridge.service(); assert(radio.requests==afterIdle);
}
void rxTest() {
  Clock clock; Fan fan; Tach tach; Store store(fan); Radio radio(clock);
  ota::Receiver receiver(store,fan,7,1,42); ota::RxMaintenance maintenance(receiver);
  RxController rx(radio,clock,fan,tach,RxControllerConfig{7,1});
  rx.setMaintenance(&maintenance); assert(rx.begin());
  control(radio,1,20); rx.runOnce(); assert(fan.duty==20);
  auto begin=command(ota::Op::Begin,128); bytes::putU32Le(begin.data.data(),42);
  queue(radio,begin); rx.runOnce(); assert(fan.duty==255 && rx.linkFailsafeActive());
  control(radio,2,10); rx.runOnce(); assert(fan.duty==255);
  clock.delayMilliseconds(2200); control(radio,3,10); rx.runOnce();
  assert(fan.duty==255 && radio.begins==1); // No no-control radio reset during OTA.
  queue(radio,command(ota::Op::Abort)); rx.runOnce(); assert(!receiver.active());
  assert(fan.duty==255);
  // Recovery may perform its existing radio begin before consuming new control.
  control(radio,1,20); rx.runOnce(); assert(fan.duty==255);
  control(radio,4,30); rx.runOnce(); assert(fan.duty==30);
  ++begin.session;
  queue(radio,begin); rx.runOnce(); assert(fan.duty==255 && receiver.active());
  clock.delayMilliseconds(10001); rx.runOnce(); assert(fan.duty==255);
}
}
int main() { txTest(); fastTxTest(); imageRateTest(false); imageRateTest(true); rxTest(); std::puts("PASS OTA TX/RX integration, 5120B/s firmware pacing including retries/wrap/no-credit, control lock"); }
