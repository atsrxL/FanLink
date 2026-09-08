#pragma once
#include <MD5Builder.h>
#include "fanlink/ota_receiver.hpp"

namespace fanlink::platform::esp8266 {
class Esp8266OtaStorage final : public ota::IUpdateStorage {
 public:
  bool begin(std::uint32_t size, const std::array<std::uint8_t, 16>& md5) override;
  bool write(const std::uint8_t* data, std::size_t length) override;
  bool verify() override;
  bool commit() override;
  void abort() override;
 private:
  MD5Builder digest_;
  std::array<std::uint8_t, 16> expected_{};
  std::uint32_t size_{0};
  std::uint32_t written_{0};
  bool running_{false};
  bool verified_{false};
};
}  // namespace fanlink::platform::esp8266
