#pragma once

#include "fanlink/interfaces.hpp"
#include "fanlink/ota_protocol.hpp"

namespace fanlink::ota {

class IUpdateStorage {
 public:
  virtual ~IUpdateStorage() = default;
  virtual bool begin(std::uint32_t size, const std::array<std::uint8_t, 16>& md5) = 0;
  virtual bool write(const std::uint8_t* data, std::size_t length) = 0;
  // Verify the entire staged image without scheduling boot.
  virtual bool verify() = 0;
  virtual bool commit() = 0;
  virtual void abort() = 0;
};

class Receiver {
 public:
  Receiver(IUpdateStorage& storage, IFanOutput& fan, std::uint32_t network,
           std::uint8_t node, std::uint32_t compatibility);
  bool process(const Packet& request, Packet& reply, std::uint32_t nowMs);
  void tick(std::uint32_t nowMs);
  bool active() const;
  bool rebootDue(std::uint32_t nowMs) const;
  State state() const { return state_; }

 private:
  void respond(const Packet& request, Packet& reply, Status status) const;
  void fail();
  IUpdateStorage& storage_;
  IFanOutput& fan_;
  const std::uint32_t network_;
  const std::uint8_t node_;
  const std::uint32_t compatibility_;
  State state_{State::Idle};
  std::uint32_t session_{0};
  std::uint32_t size_{0};
  std::uint32_t nextOffset_{0};
  std::uint32_t lastProgressMs_{0};
  std::uint32_t committedMs_{0};
  std::array<std::uint8_t, 16> md5_{};
  std::uint8_t hashOffset_{0};
  bool haveCached_{false};
  Packet lastRequest_{};
  Packet lastReply_{};
  std::array<std::uint8_t, kBlockSize> block_{};
  std::uint32_t blockStart_{0};
  std::uint16_t blockMask_{0};
  std::uint8_t dataMode_{0};  // 0 undecided, 1 legacy, 2 block
  bool blockBad_{false};
};

}  // namespace fanlink::ota
