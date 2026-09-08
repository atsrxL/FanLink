#include "fanlink/platform/esp8266/ota_storage.hpp"
#include "ota_storage_fake.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>

using fanlink::platform::esp8266::Esp8266OtaStorage;
namespace fake = ota_storage_fake;
namespace {
unsigned checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
  std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); std::exit(1); \
} } while (false)
constexpr std::size_t imageSize = 128;
std::array<std::uint8_t, 16> expectedDigest() {
  std::array<std::uint8_t, 16> value{};
  for (std::size_t i = 0; i < value.size(); ++i) value[i] = 0x80U + i;
  return value;
}
std::vector<std::uint8_t> image() {
  std::vector<std::uint8_t> value(imageSize, 0x42);
  value[0] = 0xE9;
  return value;
}
void prepare(Esp8266OtaStorage& storage) {
  fake::state.digest = expectedDigest();
  CHECK(storage.begin(imageSize, expectedDigest()));
}
void upload(Esp8266OtaStorage& storage) {
  const auto bytes = image();
  for (std::size_t offset = 0; offset < bytes.size(); offset += 10)
    CHECK(storage.write(bytes.data() + offset, std::min<std::size_t>(10, bytes.size() - offset)));
}
void checkNoCommit() { CHECK(fake::state.commits == 0); }
void abortAt(unsigned stage) {
  fake::reset();
  Esp8266OtaStorage storage;
  prepare(storage);
  if (stage == 1) {
    auto bytes = image();
    CHECK(storage.write(bytes.data(), 10));
  } else if (stage >= 2) {
    upload(storage);
    if (stage == 3) CHECK(storage.verify());
  }
  storage.abort();
  checkNoCommit();
  CHECK(!fake::state.running);
  CHECK(fake::state.endCalls == 1);
  CHECK(fake::state.calls[fake::state.calls.size() - 2] ==
        "md5:XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX");
  CHECK(fake::state.calls.back() == "end:false");
  CHECK(!storage.verify());
  CHECK(!storage.commit());
  storage.abort();
  CHECK(fake::state.endCalls == 1);
  CHECK(storage.begin(imageSize, expectedDigest()));
  storage.abort();
  checkNoCommit();
}
void beginChecks() {
  fake::reset();
  Esp8266OtaStorage storage;
  CHECK(!storage.begin(0, expectedDigest()));
  CHECK(!storage.begin(127, expectedDigest()));
  fake::state.freeSpace = imageSize;
  CHECK(!storage.begin(imageSize + 1, expectedDigest()));
  fake::state.beginFails = true;
  CHECK(!storage.begin(imageSize, expectedDigest()));
  fake::state.beginFails = false;
  prepare(storage);
  CHECK(fake::state.calls[fake::state.calls.size() - 3] == "update.begin");
  CHECK(fake::state.calls[fake::state.calls.size() - 2] ==
        "md5:808182838485868788898a8b8c8d8e8f");
  CHECK(fake::state.calls.back() == "digest.begin");
  CHECK(!storage.begin(imageSize, expectedDigest()));
  storage.abort();
  fake::state.running = true;  // Another owner must remain untouched.
  const auto calls = fake::state.calls.size();
  CHECK(!storage.begin(imageSize, expectedDigest()));
  storage.abort();
  CHECK(fake::state.running);
  CHECK(fake::state.calls.size() == calls);
  checkNoCommit();
}
void invalidWrites() {
  fake::reset();
  Esp8266OtaStorage storage;
  auto bytes = image();
  CHECK(!storage.write(bytes.data(), 10));
  prepare(storage);
  const auto calls = fake::state.calls.size();
  CHECK(!storage.write(nullptr, 10));
  CHECK(!storage.write(bytes.data(), 0));
  CHECK(!storage.write(bytes.data(), 101));
  bytes[0] = 0x1F;  // Core accepts gzip; RF adapter must reject it.
  CHECK(!storage.write(bytes.data(), 10));
  bytes[0] = 0x00;
  CHECK(!storage.write(bytes.data(), 10));
  CHECK(fake::state.calls.size() == calls);
  CHECK(!storage.verify());
  CHECK(!storage.commit());
  CHECK(fake::state.endCalls == 0);
  upload(storage);
  CHECK(!storage.write(bytes.data(), 1));
  CHECK(fake::state.hashedBytes == image());
  storage.abort();
  checkNoCommit();
}
void successfulCommit() {
  fake::reset();
  Esp8266OtaStorage storage;
  prepare(storage);
  fake::state.mutateWriteBuffer = true;
  upload(storage);
  CHECK(fake::state.hashedBytes == image());
  CHECK(fake::state.flashedBytes == image());
  CHECK(!storage.commit());
  CHECK(fake::state.endCalls == 0);
  CHECK(storage.verify());
  CHECK(storage.verify());
  CHECK(fake::state.calculateCalls == 1);
  CHECK(fake::state.endCalls == 0);
  auto bytes = image();
  CHECK(!storage.write(bytes.data(), 1));
  CHECK(storage.commit());
  CHECK(fake::state.endCalls == 1);
  CHECK(fake::state.commits == 1);
  CHECK(fake::state.calls.back() == "end:false");
  CHECK(!storage.commit());
  storage.abort();
  CHECK(fake::state.endCalls == 1);
  CHECK(fake::state.commits == 1);
}
void blockWrites() {
  for (bool cancel : {false, true}) {
    fake::reset(); Esp8266OtaStorage storage; prepare(storage);
    fake::state.mutateWriteBuffer = true;
    const auto bytes = image();
    CHECK(storage.write(bytes.data(), 100));
    CHECK(storage.write(bytes.data() + 100, 28));
    CHECK(fake::state.hashedBytes == bytes);
    CHECK(fake::state.flashedBytes == bytes);
    CHECK(storage.verify());
    if (cancel) { storage.abort(); checkNoCommit(); }
    else { CHECK(storage.commit()); CHECK(fake::state.commits == 1); }
  }
}
void failures() {
  for (unsigned failure = 0; failure < 6; ++failure) {
    fake::reset();
    Esp8266OtaStorage storage;
    if (failure == 0) {
      fake::state.nextSetMd5Fails = true;
      CHECK(!storage.begin(imageSize, expectedDigest()));
      CHECK(!fake::state.running);
      checkNoCommit();
      CHECK(storage.begin(imageSize, expectedDigest()));
    } else {
      prepare(storage);
      if (failure == 1) {
        fake::state.writeFails = true;
        const auto bytes = image();
        CHECK(!storage.write(bytes.data(), 10));
        CHECK(fake::state.hashedBytes.empty());
        CHECK(!fake::state.running);
        CHECK(!storage.verify());
        CHECK(!storage.commit());
      } else {
        upload(storage);
        if (failure == 2) fake::state.digest[0] ^= 1;
        if (failure == 3) fake::state.error = true;
        if (failure == 4) fake::state.forceUnfinished = true;
        if (failure < 5) {
          CHECK(!storage.verify());
          CHECK(!storage.commit());
          CHECK(fake::state.endCalls == 0);
        } else {
          CHECK(storage.verify());
          fake::state.endFails = true;
          CHECK(!storage.commit());
          CHECK(!fake::state.running);
          CHECK(!storage.commit());
        }
      }
    }
    storage.abort();
    checkNoCommit();
  }
}
}  // namespace
int main() {
  // Complete-image cancellation first: removing the poison must fail on the
  // observable accidental-commit invariant before checking API call order.
  for (unsigned stage : {2U, 3U, 0U, 1U}) abortAt(stage);
  beginChecks();
  invalidWrites();
  successfulCommit();
  blockWrites();
  failures();
  std::printf("PASS: actual OTA storage adapter, %u checks (fake Core failure injection)\n", checks);
}
