#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ota_storage_fake {
struct State {
  std::uint32_t freeSpace{512U * 1024U};
  bool beginFails{false};
  bool nextSetMd5Fails{false};
  bool writeFails{false};
  bool mutateWriteBuffer{false};
  bool endFails{false};
  bool running{false};
  bool error{false};
  bool forceUnfinished{false};
  std::size_t size{0};
  std::size_t written{0};
  unsigned commits{0};
  unsigned endCalls{0};
  unsigned calculateCalls{0};
  std::string expectedHex;
  std::array<std::uint8_t, 16> digest{};
  std::vector<std::uint8_t> hashedBytes;
  std::vector<std::uint8_t> flashedBytes;
  std::vector<std::string> calls;
};
inline State state;
inline std::string hexDigest() {
  constexpr char hex[] = "0123456789abcdef";
  std::string result;
  for (auto byte : state.digest) {
    result += hex[byte >> 4];
    result += hex[byte & 15];
  }
  return result;
}
inline void reset() { state = State{}; }
}  // namespace ota_storage_fake
