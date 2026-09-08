#pragma once
#include "fanlink/ota_protocol.hpp"
#include "fanlink/tx_controller.hpp"

namespace fanlink::ota {
class TxBridge {
 public:
  TxBridge(IRadioPort& radio, IClock& clock, TxController& controller,
           std::uint32_t network)
      : radio_(radio), clock_(clock), controller_(controller), network_(network) {}
  bool enqueue(const std::uint8_t* data, std::size_t length, std::uint16_t sequence);
  bool enqueueBlock(const std::uint8_t* data, std::size_t length, std::uint16_t sequence);
  // Returns true while normal control must remain suspended. One RF exchange
  // per call, bounded to 300 ms, with no hidden retransmits or fake RX ACKs.
  bool service();
  bool takeReply(std::array<std::uint8_t, 30>& data, std::uint16_t& sequence);
  bool active() const { return active_; }
 private:
  void release();
  IRadioPort& radio_;
  IClock& clock_;
  TxController& controller_;
  std::uint32_t network_;
  Packet request_{};
  std::array<std::uint8_t, 30> requestBytes_{};
  std::array<std::uint8_t, 30> replyBytes_{};
  std::uint32_t startedMs_{0};
  std::uint32_t lastRequestMs_{0};
  std::uint32_t session_{0};
  std::uint32_t imageSize_{0};
  std::uint32_t compatibility_{0};
  std::uint8_t node_{0};
  std::uint16_t sequence_{0};
  bool active_{false};
  bool pending_{false};
  bool haveReply_{false};
  std::array<std::uint8_t, kBlockSize> block_{};
  std::size_t blockCount_{0};
  std::uint32_t lastImageSendUs_{0};
  std::uint32_t imageSendIntervalUs_{0};
  bool haveImageSend_{false};
};
}  // namespace fanlink::ota
