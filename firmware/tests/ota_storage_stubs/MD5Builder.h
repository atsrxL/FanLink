#pragma once
#include "ota_storage_fake.hpp"
#include <algorithm>
class MD5Builder {
 public:
  void begin() {
    auto& state = ota_storage_fake::state;
    state.calls.emplace_back("digest.begin");
    state.hashedBytes.clear();
  }
  void add(const std::uint8_t* data, std::uint16_t length) {
    auto& state = ota_storage_fake::state;
    state.calls.emplace_back("digest.add");
    state.hashedBytes.insert(state.hashedBytes.end(), data, data + length);
  }
  void calculate() {
    ota_storage_fake::state.calls.emplace_back("digest.calculate");
    ++ota_storage_fake::state.calculateCalls;
  }
  void getBytes(std::uint8_t* out) const {
    auto& state = ota_storage_fake::state;
    state.calls.emplace_back("digest.getBytes");
    std::copy(state.digest.begin(), state.digest.end(), out);
  }
};
