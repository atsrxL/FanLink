#pragma once
#include "ota_storage_fake.hpp"
constexpr int U_FLASH = 0;
class FakeUpdater {
 public:
  bool isRunning() const { return ota_storage_fake::state.running; }
  bool hasError() const { return ota_storage_fake::state.error; }
  bool isFinished() const {
    const auto& state = ota_storage_fake::state;
    return !state.forceUnfinished && state.written == state.size;
  }
  bool begin(std::size_t size, int command) {
    auto& state = ota_storage_fake::state;
    state.calls.emplace_back("update.begin");
    if (state.beginFails || state.running || command != U_FLASH) return false;
    state.running = true;
    state.error = false;
    state.size = size;
    state.written = 0;
    state.expectedHex.clear();
    state.flashedBytes.clear();
    return true;
  }
  bool setMD5(const char* text) {
    auto& state = ota_storage_fake::state;
    state.calls.emplace_back(std::string("md5:") + text);
    if (state.nextSetMd5Fails) {
      state.nextSetMd5Fails = false;
      return false;
    }
    // Core 3.1.2 validates length only. Non-hex poison is accepted then
    // necessarily mismatches the computed digest in end(false).
    if (std::string(text).size() != 32U) return false;
    state.expectedHex = text;
    return true;
  }
  std::size_t write(std::uint8_t* data, std::size_t length) {
    auto& state = ota_storage_fake::state;
    state.calls.emplace_back("update.write");
    if (state.writeFails || !state.running || state.error) {
      state.error = true;
      state.running = false;  // Core _setError resets the updater.
      return 0;
    }
    state.flashedBytes.insert(state.flashedBytes.end(), data, data + length);
    state.written += length;
    if (state.mutateWriteBuffer && length) data[0] ^= 0xFFU;
    return length;
  }
  bool end(bool evenIfRemaining = false) {
    auto& state = ota_storage_fake::state;
    state.calls.emplace_back(evenIfRemaining ? "end:true" : "end:false");
    ++state.endCalls;
    const bool success = state.running && !state.error && !state.endFails &&
        state.written != 0 && (state.written == state.size || evenIfRemaining) &&
        state.expectedHex == ota_storage_fake::hexDigest();
    state.running = false;
    if (success) ++state.commits;
    return success;
  }
};
inline FakeUpdater Update;
