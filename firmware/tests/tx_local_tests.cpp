#include <cassert>
#include <cstdio>
#include <vector>
#include "fanlink/ota_tx_local.hpp"
#include "fanlink/tx_lifecycle.hpp"
using namespace fanlink;
struct Clock : IClock {
  std::uint32_t now{0};
  std::uint32_t millis() const override { return now; }
  std::uint32_t micros() const override { return now * 1000; }
  void delayMilliseconds(std::uint32_t n) override { now += n; }
  void delayMicroseconds(std::uint32_t n) override { now += (n + 999) / 1000; }
};
struct Radio : IRadioPort {
  Clock& clock; unsigned calls{0};
  explicit Radio(Clock& c):clock(c) {}
  bool begin(std::uint32_t) override { ++calls; return true; }
  RadioResult transmit(const std::uint8_t* data, std::size_t len) override {
    ++calls; ControlPacket p{}; assert(decodeControlPacket(data,len,p));
    for(auto duty:p.duty) assert(duty==255); return RadioResult::Ok;
  }
  RadioRxResult receive(std::uint8_t*,std::size_t,std::uint32_t ms) override {
    ++calls; clock.now+=ms; return {RadioResult::Timeout};
  }
  int lastError() const override { return 0; }
};
struct Storage : ota::IUpdateStorage {
  std::vector<std::uint8_t> data; unsigned commits{0}, begins{0}, aborts{0};
  bool begin(std::uint32_t,const std::array<std::uint8_t,16>&) override { ++begins; data.clear(); return true; }
  bool write(const std::uint8_t* p,std::size_t n) override { data.insert(data.end(),p,p+n); return true; }
  bool verify() override { return true; }
  bool commit() override { ++commits; return true; }
  void abort() override { ++aborts; }
};
ota::Packet packet(ota::Op op,std::uint32_t offset=0) {
  ota::Packet p{}; p.networkId=7; p.nodeId=1; p.session=9; p.operation=op; p.offset=offset; return p;
}
void local() {
  Clock c; Radio r(c); TxController tx(r,c,TxControllerConfig{7}); assert(tx.begin());
  Storage s; ota::TxLocalUpdate u(s,c,tx,7,1,42);
  auto p=packet(ota::Op::Begin,128); bytes::putU32Le(p.data.data(),42);
  auto enqueue=[&](const ota::Packet& command) {
    std::array<std::uint8_t,30> b{}; assert(ota::encodeRequest(command,b));
    return u.enqueue(b.data(),b.size(),123,false);
  };
  auto response=[&]() {
    assert(u.service()); std::array<std::uint8_t,30> b{}; std::uint16_t seq=0;
    assert(u.takeReply(b,seq)); assert(seq==123); ota::Packet result{};
    assert(ota::decodeReply(b.data(),b.size(),result)); return result;
  };
  auto bad=p; bad.data[4]=1; assert(!enqueue(bad)); assert(!u.active());
  bad=p; bad.nodeId=2; assert(!enqueue(bad));
  tx.setNodeDuty(1,0); assert(enqueue(p)); assert(tx.requestedDuty()[0]==255);
  while(c.now<600) { assert(u.service()); ++c.now; } assert(s.begins==0);
  const auto radioCalls=r.calls;
  assert(response().data[0]==0);
  assert(enqueue(packet(ota::Op::Hash))); assert(response().data[0]==0);
  assert(enqueue(packet(ota::Op::Hash,10))); assert(response().data[0]==0);
  assert(s.begins==1);
  for(unsigned offset=0;offset<128;offset+=100) {
    auto fragment=packet(ota::Op::Fragment,offset); fragment.data.fill(3);
    std::array<std::uint8_t,120> b{}; std::array<std::uint8_t,30> encoded{};
    assert(ota::encodeRequest(fragment,encoded)); std::copy(encoded.begin(),encoded.end(),b.begin());
    const unsigned count=std::min(100U,128-offset); std::fill(b.begin()+30,b.begin()+30+count-10,3);
    assert(u.enqueue(b.data(),30+count-10,123,true)); assert(response().offset==offset+count);
    assert(u.enqueue(b.data(),30+count-10,123,true)); assert(response().offset==offset+count);
  }
  assert(s.data.size()==128); assert(r.calls==radioCalls);
  assert(enqueue(packet(ota::Op::Finish,128))); assert(response().data[0]==0);
  assert(enqueue(packet(ota::Op::Commit,128))); assert(response().data[0]==0);
  assert(s.commits==1); assert(!u.rebootDue()); c.now+=2499; assert(!u.rebootDue());
  ++c.now; assert(u.rebootDue()); assert(r.calls==radioCalls);
  Clock c2; Radio r2(c2); TxController tx2(r2,c2,TxControllerConfig{7});
  Storage s2; ota::TxLocalUpdate timeout(s2,c2,tx2,7,1,42);
  std::array<std::uint8_t,30> encoded{}; assert(ota::encodeRequest(p,encoded));
  assert(timeout.enqueue(encoded.data(),30,1,false)); c2.now=600; assert(timeout.service());
  std::uint16_t seq; assert(timeout.takeReply(encoded,seq)); c2.now=10600;
  assert(timeout.service()); assert(!timeout.active()); assert(tx2.requestedDuty()[0]==255);
  assert(ota::encodeRequest(p,encoded)); assert(!timeout.enqueue(encoded.data(),30,1,false));
  p.session=10; assert(ota::encodeRequest(p,encoded));
  assert(timeout.enqueue(encoded.data(),30,1,false)); c2.now+=600;
  assert(timeout.service()); assert(timeout.takeReply(encoded,seq));
  auto abort=packet(ota::Op::Abort); abort.session=10; assert(ota::encodeRequest(abort,encoded));
  assert(timeout.enqueue(encoded.data(),30,1,false)); assert(timeout.service());
  assert(!timeout.active()); assert(timeout.takeReply(encoded,seq));
  const auto abortCount=s2.aborts; assert(ota::encodeRequest(abort,encoded));
  assert(timeout.enqueue(encoded.data(),30,2,false)); assert(timeout.service());
  assert(!timeout.active()); assert(timeout.takeReply(encoded,seq)); assert(seq==2);
  assert(s2.aborts==abortCount);
}
void identity() {
  std::array<std::uint8_t,28> id{{'F','A','N','L','I','N','K','-','O','T','A','-','V','1','!','!'}};
  id[24]=1; id[25]=1; id[26]=1;
  for(unsigned split=1;split<28;++split) {
    Storage s; ota::IdentityStorage guard(s,id); assert(guard.begin(128,{}));
    assert(guard.write(id.data(),split)); assert(guard.write(id.data()+split,28-split));
    assert(guard.verify()); assert(guard.commit());
    assert(guard.write(id.data(),28)); assert(!guard.verify()); assert(!guard.commit());
    assert(guard.begin(128,{})); auto wrong=id; wrong[24]=2;
    assert(guard.write(wrong.data(),28)); assert(!guard.verify()); assert(!guard.commit());
    assert(guard.begin(128,{})); assert(!guard.verify());
    assert(guard.write(id.data(),28)); assert(guard.write(id.data(),16)); assert(!guard.verify());
  }
}
void lifecycle() {
  Clock c; Radio r(c); TxController tx(r,c,TxControllerConfig{7});
  assert(tx.begin());
  TxLifecycle life(c,tx,7); SerialFrame req{}; req.type=0x14; req.sequence=8; req.payloadLength=16;
  bytes::putU32Le(req.payload.data(),7); bytes::putU32Le(req.payload.data()+4,9); req.payload[8]=2;
  const char guard[]="FLTXCTL"; std::copy_n(guard,7,req.payload.begin()+9);
  auto bad=req; bad.payload[15]=0; assert(!life.enqueue(bad,false)); assert(!life.active());
  assert(life.enqueue(req,true)); SerialFrame ack{}; assert(life.replyReady(ack));
  assert(ack.payload[16]==1); life.replyWritten(true); assert(!life.active()); assert(!life.dueAction());
  tx.setNodeDuty(1,0); assert(life.enqueue(req,false)); assert(tx.requestedDuty()[0]==255);
  assert(!life.replyReady(ack)); while(c.now<600) { assert(life.service()); ++c.now; }
  const auto calls=r.calls; assert(life.replyReady(ack)); assert(ack.type==0x92 && ack.sequence==8);
  life.replyWritten(false); c.now+=300; assert(!life.dueAction());
  assert(life.enqueue(req,false)); assert(life.replyReady(ack)); life.replyWritten(true);
  c.now+=249; assert(!life.dueAction()); ++c.now; assert(life.dueAction()==2);
  assert(life.service()); assert(r.calls==calls);
  auto conflict=req; conflict.payload[8]=1;
  assert(life.enqueue(conflict,false)); assert(life.replyReady(ack)); assert(ack.payload[16]==1);
  life.replyWritten(true); assert(life.dueAction()==2);
  TxLifecycle timeout(c,tx,7); assert(timeout.enqueue(req,false)); c.now+=10000;
  assert(timeout.service()); assert(!timeout.active()); assert(!timeout.dueAction());
  assert(timeout.enqueue(req,false)); assert(timeout.active());
  c.now=UINT32_MAX-200; TxLifecycle wrap(c,tx,7); assert(wrap.enqueue(req,false));
  c.now+=600; assert(wrap.replyReady(ack)); wrap.replyWritten(true); c.now+=250;
  assert(wrap.dueAction()==2);
}
int main() { local(); identity(); lifecycle(); std::puts("PASS: TX local update, identity, lifecycle regressions"); }
