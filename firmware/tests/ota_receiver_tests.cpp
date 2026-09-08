#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "fanlink/byte_utils.hpp"
#include "fanlink/crc16.hpp"
#include "fanlink/ota_receiver.hpp"
#include "fanlink/protocol.hpp"

namespace {
using namespace fanlink::ota;
constexpr std::uint32_t kNetwork = 0x01020304U;
constexpr std::uint32_t kCompatibility = 0x12345678U;
unsigned checks = 0;
void require(bool value, const char* message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
bool same(const Packet& a, const Packet& b) {
  return a.networkId == b.networkId && a.nodeId == b.nodeId && a.session == b.session &&
         a.operation == b.operation && a.offset == b.offset && a.data == b.data;
}
struct Fan final : fanlink::IFanOutput {
  std::uint8_t duty{0};
  unsigned fullCalls{0};
  bool begin() override { return true; }
  void setDuty(std::uint8_t value) override { duty = value; }
  void setFullSpeed() override { duty = 255; ++fullCalls; }
  std::uint8_t appliedDuty() const override { return duty; }
};
struct Storage final : IUpdateStorage {
  Fan& fan;
  unsigned begins{0}, writes{0}, verifies{0}, commits{0}, aborts{0};
  bool beginOk{true}, writeOk{true}, verifyOk{true}, commitOk{true};
  std::uint32_t size{0};
  std::array<std::uint8_t, 16> md5{};
  std::vector<std::uint8_t> image;
  explicit Storage(Fan& output) : fan(output) {}
  bool begin(std::uint32_t length, const std::array<std::uint8_t, 16>& hash) override {
    require(fan.duty == 255U, "fan must be full speed before staging");
    ++begins; size = length; md5 = hash; image.clear(); return beginOk;
  }
  bool write(const std::uint8_t* data, std::size_t length) override {
    require(fan.duty == 255U, "fan must remain full speed during writes");
    ++writes; if (writeOk) image.insert(image.end(), data, data + length); return writeOk;
  }
  bool verify() override { ++verifies; return verifyOk && image.size() == size; }
  bool commit() override { ++commits; return commitOk; }
  void abort() override { ++aborts; }
};
struct Rig {
  Fan fan;
  Storage storage{fan};
  Receiver rx{storage, fan, kNetwork, 1U, kCompatibility};
  std::uint32_t now{100};
  std::uint32_t size{13};
  std::uint32_t session{7};
  Packet make(Op op, std::uint32_t offset = 0) const {
    Packet packet{};
    packet.networkId = kNetwork; packet.session = session; packet.nodeId = 1;
    packet.operation = op; packet.offset = offset;
    if (op == Op::Begin) fanlink::bytes::putU32Le(packet.data.data(), kCompatibility);
    return packet;
  }
  Packet send(const Packet& request, Status expected = Status::Ok) {
    Packet reply{};
    require(rx.process(request, reply, now++), "matching request must get reply");
    require(reply.data[0] == static_cast<std::uint8_t>(expected), "unexpected status");
    require(reply.session == request.session && reply.operation == request.operation,
            "reply must echo session/operation");
    require(fanlink::bytes::getU32Le(reply.data.data() + 2) == kCompatibility,
            "reply compatibility mismatch");
    return reply;
  }
  Packet begin() { const auto packet = make(Op::Begin, size); send(packet); return packet; }
  Packet hash(std::uint32_t offset) {
    auto packet = make(Op::Hash, offset);
    const unsigned count = offset == 0 ? 10 : 6;
    for (unsigned i = 0; i < count; ++i) packet.data[i] = static_cast<std::uint8_t>(offset + i);
    return packet;
  }
  void receiving() { begin(); send(hash(0)); send(hash(10)); }
  Packet data(std::uint32_t offset) {
    auto packet = make(Op::Data, offset);
    for (std::uint32_t i = 0; i < std::min(10U, size - offset); ++i)
      packet.data[i] = static_cast<std::uint8_t>(offset + i + 1);
    return packet;
  }
  void verified() {
    receiving(); send(data(0)); send(data(10)); send(make(Op::Finish, size));
  }
};
void codec() {
  Rig rig;
  auto request = rig.make(Op::Begin, 0x12345);
  std::array<std::uint8_t, 30> bytes{};
  require(encodeRequest(request, bytes), "request encode");
  require(bytes[0] == 0xF1 && bytes[1] == 0xA5 && bytes[2] == 1 && bytes[3] == 0x30,
          "request fixed header");
  require(bytes[4] == 4 && bytes[7] == 1 && bytes[8] == 7 && bytes[12] == 1 &&
          bytes[14] == 0x45 && bytes[15] == 0x23 && bytes[18] == 0x78, "wire LE offsets");
  Packet decoded;
  require(decodeRequest(bytes.data(), bytes.size(), decoded) && same(request, decoded),
          "request roundtrip");
  require(!decodeReply(bytes.data(), bytes.size(), decoded), "reject request as reply");
  require(!decodeRequest(nullptr, 30, decoded), "null input");
  for (std::size_t length = 0; length <= 31; ++length)
    if (length != 30) require(!decodeRequest(bytes.data(), length, decoded), "exact length");
  for (std::size_t byte = 0; byte < bytes.size(); ++byte) {
    auto corrupt = bytes; corrupt[byte] ^= 1;
    require(!decodeRequest(corrupt.data(), corrupt.size(), decoded), "single byte corruption");
  }
  for (unsigned field : {0U, 1U, 2U, 3U, 12U, 13U}) {
    auto corrupt = bytes; corrupt[field] = 0;
    fanlink::bytes::putU16Le(corrupt.data() + 28,
                           fanlink::crc16CcittFalse(corrupt.data(), 28));
    require(!decodeRequest(corrupt.data(), 30, decoded), "invalid header despite valid CRC");
  }
  auto invalid = request; invalid.session = 0;
  require(!encodeRequest(invalid, bytes), "session zero");
  invalid = request; invalid.nodeId = 17;
  require(!encodeRequest(invalid, bytes), "node above range");
  auto reply = rig.send(request);
  require(encodeReply(reply, bytes) && decodeReply(bytes.data(), 30, decoded) &&
          same(reply, decoded), "reply roundtrip");
  require(bytes[3] == 0x31, "reply type");
  require(!decodeRequest(bytes.data(), 30, decoded), "reject reply as request");
  reply.data[6] = 1;
  require(!encodeReply(reply, bytes), "reply reserved zero");
  reply.data[6] = 0; reply.data[0] = 7;
  require(!encodeReply(reply, bytes), "invalid status");
  reply.data[0] = 0; reply.data[1] = 6;
  require(!encodeReply(reply, bytes), "invalid state");
}
void eightNodeBoundary() {
  static_assert(fanlink::kNodeCount == 8U, "OTA supports exactly eight receivers");
  Rig rig;
  auto request = rig.make(Op::Begin, rig.size);
  request.nodeId = 8U;
  Fan fan;
  Storage storage{fan};
  Receiver receiver{storage, fan, kNetwork, 8U, kCompatibility};
  Packet reply{}, decoded{};
  std::array<std::uint8_t, 30> requestBytes{}, replyBytes{};
  require(encodeRequest(request, requestBytes) &&
          decodeRequest(requestBytes.data(), requestBytes.size(), decoded) &&
          same(request, decoded), "node eight request codec accepted");
  require(receiver.process(decoded, reply, rig.now) && receiver.active() &&
          receiver.state() == State::Manifest && fan.duty == 255U,
          "node eight may enter protected maintenance");
  require(encodeReply(reply, replyBytes) &&
          decodeReply(replyBytes.data(), replyBytes.size(), decoded) && same(reply, decoded),
          "node eight reply codec accepted");
  require(requestBytes[2] == 1U && replyBytes[2] == 1U,
          "OTA retains RF version one and thirty-byte envelopes");
  for (std::uint8_t node : {0U, 9U, 16U, 17U, 255U}) {
    auto invalidRequest = request; invalidRequest.nodeId = node;
    auto invalidReply = reply; invalidReply.nodeId = node;
    auto scratch = requestBytes;
    require(!encodeRequest(invalidRequest, scratch) && scratch == requestBytes,
            "invalid node request encode preserves output");
    scratch = replyBytes;
    require(!encodeReply(invalidReply, scratch) && scratch == replyBytes,
            "invalid node reply encode preserves output");
    for (bool isReply : {false, true}) {
      auto corrupt = isReply ? replyBytes : requestBytes;
      corrupt[12] = node;
      fanlink::bytes::putU16Le(corrupt.data() + 28,
                             fanlink::crc16CcittFalse(corrupt.data(), 28));
      decoded = request;
      const bool accepted = isReply ? decodeReply(corrupt.data(), corrupt.size(), decoded)
                                    : decodeRequest(corrupt.data(), corrupt.size(), decoded);
      require(!accepted && same(decoded, request),
              "invalid node with valid CRC rejected without output mutation");
    }
    // Direct calls must reject invalid local identity too, even without the codec.
    Fan invalidFan;
    Storage invalidStorage{invalidFan};
    Receiver invalidReceiver{invalidStorage, invalidFan, kNetwork, node, kCompatibility};
    auto untouched = reply;
    require(!invalidReceiver.process(invalidRequest, untouched, rig.now) &&
            same(untouched, reply), "invalid local node cannot reply to matching request");
    require(!invalidReceiver.active() && invalidReceiver.state() == State::Idle &&
            invalidStorage.begins == 0U && invalidStorage.writes == 0U &&
            invalidStorage.commits == 0U && invalidFan.fullCalls == 0U,
            "invalid local node cannot enter maintenance or touch storage or fan");
    require(!rig.rx.process(invalidRequest, untouched, rig.now) && !rig.rx.active() &&
            rig.storage.begins == 0U, "invalid target cannot enter valid node maintenance");
  }
}
void identityAndManifest() {
  Rig r;
  Packet reply;
  auto packet = r.make(Op::Begin, r.size);
  packet.networkId++;
  require(!r.rx.process(packet, reply, r.now), "wrong network silent");
  packet = r.make(Op::Begin, r.size); packet.nodeId++;
  require(!r.rx.process(packet, reply, r.now), "wrong node silent");
  packet = r.make(Op::Begin, r.size); packet.session = 0;
  require(!r.rx.process(packet, reply, r.now), "zero session silent");
  packet = r.make(Op::Begin, r.size); packet.operation = static_cast<Op>(99);
  require(!r.rx.process(packet, reply, r.now), "unknown op silent");
  require(!r.rx.process(r.make(Op::Data), reply, r.now), "no session data silent");
  for (auto size : {0U, kMaxImageSize + 1U, UINT32_MAX})
    r.send(r.make(Op::Begin, size), Status::BadImage);
  packet = r.make(Op::Begin, r.size); packet.data[9] = 1;
  r.send(packet, Status::BadImage);
  packet = r.make(Op::Begin, r.size); packet.data[0] ^= 1;
  r.send(packet, Status::BadTarget);
  require(!r.rx.active() && r.storage.begins == 0, "invalid begin cannot allocate");
  const auto begin = r.begin();
  require(r.rx.active() && r.rx.state() == State::Manifest && r.fan.duty == 255 &&
          r.storage.begins == 0, "begin protects before hash allocation");
  packet = r.make(Op::Data); packet.session++;
  require(!r.rx.process(packet, reply, r.now), "wrong active session silent");
  packet = begin; packet.session++;
  r.send(packet, Status::Busy);
  packet = begin; packet.offset++;
  r.send(packet, Status::BadState);
  r.send(r.hash(10), Status::BadOffset);
  r.send(r.make(Op::Data), Status::BadState);
  r.send(r.make(Op::Commit, r.size), Status::BadState);
  r.send(r.hash(0));
  packet = r.hash(10); packet.data[6] = 1;
  r.send(packet, Status::BadImage);
  packet = r.hash(0); packet.data[0] ^= 1;
  r.send(packet, Status::BadImage);
  packet = r.hash(1);
  r.send(packet, Status::BadOffset);
  r.send(r.hash(10));
  require(r.storage.begins == 1 && r.storage.md5[0] == 0 && r.storage.md5[15] == 15 &&
          r.storage.size == 13, "correct complete manifest to storage");
  Rig maximum; maximum.size = kMaxImageSize; maximum.begin();
  require(maximum.rx.state() == State::Manifest, "upper size boundary accepted");
}
void retryAndOrder() {
  Rig r;
  auto request = r.begin();
  auto ack = r.send(request);
  require(r.fan.fullCalls == 1, "begin retry no reinitialization");
  request = r.hash(0); ack = r.send(request);
  require(same(ack, r.send(request)), "first hash cached ack");
  request = r.hash(10); ack = r.send(request);
  require(same(ack, r.send(request)) && r.storage.begins == 1, "hash retry no allocation");
  r.send(r.hash(0), Status::BadState);
  r.send(r.data(10), Status::BadOffset);
  r.send(r.make(Op::Finish, r.size), Status::BadOffset);
  request = r.data(0); ack = r.send(request);
  require(ack.offset == 10 && same(ack, r.send(request)) && r.storage.writes == 1,
          "data lost ack idempotent");
  request.data[5] ^= 1; r.send(request, Status::BadImage);
  request = r.data(10); request.data[3] = 1; r.send(request, Status::BadImage);
  r.send(r.data(9), Status::BadOffset);
  r.send(r.make(Op::Data, UINT32_MAX), Status::BadOffset);
  request = r.data(10); ack = r.send(request);
  require(ack.offset == 13 && same(ack, r.send(request)) && r.storage.writes == 2,
          "final short write retry idempotent");
  r.send(r.data(0), Status::BadOffset);
  r.send(r.make(Op::Data, r.size), Status::BadOffset);
  r.send(r.make(Op::Finish, r.size + 1), Status::BadOffset);
  request = r.make(Op::Finish, r.size); request.data[0] = 1;
  r.send(request, Status::BadImage);
  request = r.make(Op::Finish, r.size); ack = r.send(request);
  require(same(ack, r.send(request)) && r.storage.verifies == 1 && r.storage.commits == 0 &&
          !r.rx.rebootDue(r.now + 99999), "finish verifies exactly once without boot");
  require(r.storage.image.size() == 13 && r.storage.image.front() == 1 &&
          r.storage.image.back() == 13, "complete staged bytes");
  r.send(r.make(Op::Commit, r.size + 1), Status::BadOffset);
  request = r.make(Op::Commit, r.size); request.data[9] = 1;
  r.send(request, Status::BadImage);
  const auto committed = r.now;
  request = r.make(Op::Commit, r.size); ack = r.send(request);
  r.now = committed + 2000;
  require(same(ack, r.send(request)) && r.storage.commits == 1, "commit retry exactly once");
  auto altered = request; altered.data[2] = 1;
  r.send(altered, Status::BadImage);
  r.send(r.make(Op::Abort), Status::BadState);
  r.rx.tick(committed + 20000);
  require(r.rx.state() == State::Committed && r.storage.aborts == 0,
          "committed image cannot abort or expire");
  require(!r.rx.rebootDue(committed + 2499) && r.rx.rebootDue(committed + 2500),
          "reboot grace anchored at first commit");
  require(same(ack, r.send(request)), "intervening rejected request retains commit ack");
}
void timeoutsAndAbort() {
  for (bool wrapped : {false, true}) {
    Rig r; r.now = wrapped ? UINT32_MAX - 4999 : 100;
    const auto start = r.now;
    const auto begin = r.begin();
    r.now = start + 9999;
    r.send(begin);
    require(r.rx.state() == State::Manifest, "retry just before timeout allowed");
    r.now = start + 10000;
    r.send(begin, Status::BadState);
    require(r.rx.state() == State::Failed && !r.rx.active() && r.storage.aborts == 1 &&
            r.fan.duty == 255, "duplicates cannot renew timeout across wrap");
    r.rx.tick(start + 20000);
    require(r.storage.aborts == 1, "timeout abort only once");
  }
  Rig r; r.receiving();
  const auto lastProgress = r.now - 1;
  r.now = lastProgress + 9900;
  r.send(r.make(Op::Data, 123), Status::BadOffset);
  r.rx.tick(lastProgress + 10000);
  require(r.rx.state() == State::Failed, "invalid packet does not renew timeout");
  r.send(r.data(0), Status::BadState);
  r.send(r.make(Op::Begin, r.size), Status::BadState);
  ++r.session;
  r.begin();
  auto abort = r.make(Op::Abort); abort.offset = 1;
  r.send(abort, Status::BadOffset);
  abort.offset = 0; abort.data[0] = 1; r.send(abort, Status::BadImage);
  abort.data[0] = 0;
  const auto ack = r.send(abort);
  require(same(ack, r.send(abort)) && r.storage.aborts == 2 &&
          !r.rx.active() && r.rx.state() == State::Idle, "abort idempotent and exits");
  r.send(r.make(Op::Begin, r.size), Status::BadState);
  ++r.session;
  r.begin();
  require(r.rx.state() == State::Manifest, "new session may restart aborted transfer");
  Rig verified; verified.verified();
  verified.rx.tick(verified.now - 1 + 10000);
  require(verified.rx.state() == State::Failed && verified.storage.commits == 0,
          "verified uncommitted image expires without boot");
  Rig wrap; wrap.now = UINT32_MAX - 100; wrap.verified();
  const auto committed = wrap.now;
  wrap.send(wrap.make(Op::Commit, wrap.size));
  require(!wrap.rx.rebootDue(committed + 2499) && wrap.rx.rebootDue(committed + 2500),
          "commit grace wraps safely");
}
void shortImagesAndProgress() {
  for (std::uint32_t size : {1U, 9U, 10U, 11U, 20U}) {
    Rig r; r.size = size; r.receiving();
    for (std::uint32_t offset = 0; offset < size; offset += 10) {
      const auto lastProgress = r.now - 1;
      r.now = lastProgress + 9999;
      const auto packet = r.data(offset);
      r.send(packet);
      r.rx.tick(lastProgress + 10000);
      require(r.rx.state() == State::Receiving, "real progress renews timeout");
    }
    r.send(r.make(Op::Finish, size));
    require(r.storage.image.size() == size && r.storage.image.front() == 1 &&
            r.storage.image.back() == size, "short or exact chunk image correct");
    r.send(r.make(Op::Abort));
    require(r.rx.state() == State::Idle && r.storage.commits == 0,
            "verified image can still abort");
  }
}
void storageFailures() {
  for (unsigned failure = 0; failure < 4; ++failure) {
    Rig r;
    if (failure == 0) {
      r.storage.beginOk = false;
      r.begin(); r.send(r.hash(0)); r.send(r.hash(10), Status::StorageError);
    } else {
      r.receiving();
      if (failure == 1) {
        r.storage.writeOk = false; r.send(r.data(0), Status::StorageError);
      } else {
        r.send(r.data(0)); r.send(r.data(10));
        if (failure == 2) {
          r.storage.verifyOk = false;
          r.send(r.make(Op::Finish, r.size), Status::BadImage);
        } else {
          r.send(r.make(Op::Finish, r.size)); r.storage.commitOk = false;
          r.send(r.make(Op::Commit, r.size), Status::StorageError);
        }
      }
    }
    require(r.rx.state() == State::Failed && r.storage.aborts == 1 && r.fan.duty == 255 &&
            !r.rx.rebootDue(r.now + 999999), "storage failure fails closed without reboot");
    r.send(r.make(Op::Commit, r.size), Status::BadState);
    require(r.storage.aborts == 1 && r.storage.commits <= 1, "failed retries cannot commit");
    r.send(r.make(Op::Begin, r.size), Status::BadState);
    ++r.session;
    r.begin();
    require(r.rx.state() == State::Manifest, "new session may restart failed transfer");
  }
}
void fastBlocks() {
  for (unsigned size : {1U, 9U, 10U, 99U, 100U, 101U, 199U, 207U, 4097U}) {
    Rig r; r.size = size; r.receiving();
    Packet ignored{};
    for (unsigned base = 0; base < size; base += 100) {
      const auto end = std::min(base + 100U, size);
      for (unsigned offset = base; offset < end; offset += 10) {
        auto p = r.data(offset); p.operation = Op::Fragment;
        require(!r.rx.process(p, ignored, r.now++), "fragment has no ACK");
        require(!r.rx.process(p, ignored, r.now++), "duplicate fragment has no ACK");
        require(r.storage.image.size() == base, "partial block never written");
      }
      const auto finish = r.make(Op::BlockFinish, end);
      r.send(finish); r.send(finish);
      require(r.storage.image.size() == end, "block ACK duplicate writes once");
      auto replay = r.data(base); replay.operation = Op::Fragment;
      require(!r.rx.process(replay, ignored, r.now++), "post-write identical fragment silent");
      r.send(finish);
    }
    for (unsigned i = 0; i < size; ++i)
      require(r.storage.image[i] == static_cast<std::uint8_t>(i + 1), "exact block bytes");
    r.send(r.make(Op::Finish, size)); r.send(r.make(Op::Commit, size));
    require(r.storage.commits == 1, "fast image commits once");
  }
  {
    Rig r; r.size = 100; r.receiving(); Packet ignored{};
    for (unsigned offset = 10; offset < 100; offset += 10) {
      auto p = r.data(offset); p.operation = Op::Fragment;
      require(!r.rx.process(p, ignored, r.now++), "out-of-order block fragment silent");
    }
    r.send(r.make(Op::BlockFinish, 100), Status::BadOffset);
    require(r.storage.writes == 0, "missing fragment cannot write");
    auto p = r.data(0); p.operation = Op::Fragment;
    require(!r.rx.process(p, ignored, r.now++), "fill gap");
    r.send(r.make(Op::BlockFinish, 100));
    p.data[0] ^= 1;
    require(!r.rx.process(p, ignored, r.now++), "altered replay silent");
    r.send(r.make(Op::BlockFinish, 100), Status::BadImage);
    require(r.storage.writes == 1, "altered replay cannot reuse cached ACK or write");
  }
  {
    Rig r; r.receiving(); Packet ignored{};
    auto p = r.data(0); p.operation = Op::Fragment;
    const auto started = r.now;
    require(!r.rx.process(p, ignored, r.now++), "partial fragment accepted silently");
    r.send(r.data(0), Status::BadState);
    r.rx.tick(started + kProgressTimeoutMs);
    require(r.rx.state() == State::Failed && r.storage.writes == 0,
            "partial fragments do not renew progress timeout");
  }
  {
    Rig r; r.receiving(); r.send(r.data(0)); Packet ignored{};
    auto p = r.data(10); p.operation = Op::Fragment;
    require(!r.rx.process(p, ignored, r.now++), "legacy cannot switch to block mode");
    r.send(r.make(Op::BlockFinish, 13), Status::BadState);
  }
}
}  // namespace
int main() {
  try {
    codec(); eightNodeBoundary(); identityAndManifest(); retryAndOrder(); timeoutsAndAbort(); shortImagesAndProgress(); storageFailures(); fastBlocks();
    std::cout << "PASS: " << checks << " OTA checks\n";
  } catch (const std::exception& error) {
    std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
    return 1;
  }
}
