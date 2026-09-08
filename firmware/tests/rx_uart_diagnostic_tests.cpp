#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include "fanlink/rx_uart_diagnostic.hpp"

using namespace fanlink;
struct Clock : IClock {
  std::uint32_t us{0};
  std::uint32_t millis() const override { return us / 1000; }
  std::uint32_t micros() const override { return us; }
  void delayMilliseconds(std::uint32_t ms) override { us += ms * 1000; }
  void delayMicroseconds(std::uint32_t amount) override { us += amount; }
};
struct Radio : IRadioPort {
  std::array<std::uint8_t, kAirPacketSize> packet{};
  RadioRxResult result{RadioResult::Timeout, 0, -127, 0};
  unsigned writes{0};
  RpmPacket reply{};
  bool begin(std::uint32_t) override { return true; }
  RadioResult transmit(const std::uint8_t* data, std::size_t size) override {
    ++writes;
    assert(decodeRpmPacket(data, size, reply));
    return RadioResult::Ok;
  }
  RadioRxResult receive(std::uint8_t* data, std::size_t size, std::uint32_t) override {
    std::memcpy(data, packet.data(), size < packet.size() ? size : packet.size());
    return result;
  }
  int lastError() const override { return 0; }
};
struct Serial {
  int free{128};
  std::vector<std::string> lines;
  int availableForWrite() const { return free; }
  std::size_t write(const std::uint8_t* data, std::size_t size) {
    assert(size < static_cast<std::size_t>(free));
    assert(size < diagnostic::Logger::kLineCapacity);
    lines.emplace_back(reinterpret_cast<const char*>(data), size);
    return size;
  }
};
int main() {
  diagnostic::NoFan fan;
  diagnostic::NoTach tach;
  assert(fan.begin() && tach.begin());
  for (unsigned i = 0; i != 256; ++i) { fan.setDuty(i); assert(fan.appliedDuty() == 255); }
  fan.setFullSpeed();
  assert(fan.appliedDuty() == 255 && !tach.sample(1).valid && tach.sample(1).rpm == 0);
  Clock clock;
  Radio hardware;
  diagnostic::ObservedRadio radio(hardware);
  RxController controller(radio, clock, fan, tach, RxControllerConfig{1234, 2});
  assert(controller.begin());
  ControlPacket packet{};
  packet.networkId = 1234;
  packet.duty.fill(12);
  for (unsigned poll = 1; poll <= kNodeCount; ++poll) {
    packet.pollId = poll;
    packet.sequence = poll;
    assert(encodeControlPacket(packet, hardware.packet));
    hardware.result = {RadioResult::Ok, kAirPacketSize, -61, 0};
    controller.runOnce();
    clock.us += kNormalSlotPeriodUs;
  }
  assert(hardware.writes == 1 && hardware.reply.nodeId == 2);
  assert(hardware.reply.appliedDuty == 255 && hardware.reply.rpm == 0);
  assert((hardware.reply.status & node_status::kTachTimeout) != 0);
  assert(controller.radioDiagnostics().ownPolls == 1 && radio.ok == kNodeCount);
  assert(radio.rawSequence == kNodeCount && radio.rawPoll == kNodeCount && radio.rawRssi == -61);
  std::uint8_t buffer[kAirPacketSize];
  for (auto state : {RadioResult::Timeout, RadioResult::CrcError, RadioResult::IoError}) {
    hardware.result.result = state;
    radio.receiveForUs(buffer, sizeof(buffer), 500);
  }
  assert(radio.timeout == 1 && radio.crc == 1 && radio.io == 1);
  diagnostic::Logger logger;
  Serial serial;
  logger.noteLoop(UINT32_MAX - 9);
  logger.noteLoop(10);  // wrap-safe 20 us gap
  assert(!logger.service(serial, 249, controller, radio));
  assert(serial.lines.empty());
  serial.free = 0;
  assert(logger.service(serial, 250, controller, radio));
  serial.free = 128;
  assert(!logger.service(serial, 251, controller, radio));
  assert(serial.lines.empty()); // no retry spin
  for (unsigned now = 500; now <= 1750; now += 250) logger.service(serial, now, controller, radio);
  assert(serial.lines.size() == 6);
  assert(serial.lines[0].find("RXDIAG n2 nofan=1 notach=1 ota=off") == 0);
  assert(serial.lines[0].find("g20 rst") != std::string::npos);
  // Worst-case decimal sizes, including sticky signed driver error.
  auto& d = const_cast<RxRadioDiagnostics&>(controller.radioDiagnostics());
  d = {UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, INT32_MIN,
       UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX,
       UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX};
  radio.ok = radio.timeout = radio.crc = radio.io = UINT32_MAX;
  radio.rawSequence = UINT16_MAX; radio.rawPoll = UINT8_MAX; radio.rawRssi = INT8_MIN;
  for (unsigned i = 0; i < 6; ++i) logger.service(serial, UINT32_MAX - 1500 + i * 250, controller, radio);
  assert(serial.lines.size() == 12);
  for (const auto& line : serial.lines) assert(line.back() == '\n' && line.size() < 120);
  logger.service(serial, 10, controller, radio); // log timestamp wrap
  assert(serial.lines.size() == 13);
  std::cout << "PASS: RX UART diagnostic adapters, Node 2 poll, counters, bounded nonblocking log and wrap tests\n";
}
