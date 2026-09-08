#include "fanlink/platform/esp8266/ota_storage.hpp"
#include <Arduino.h>
#include <Updater.h>
#include <algorithm>

namespace fanlink::platform::esp8266 {
bool Esp8266OtaStorage::begin(std::uint32_t size,
                             const std::array<std::uint8_t, 16>& md5) {
  if (running_ || Update.isRunning() || size < 128U ||
      size > ESP.getFreeSketchSpace()) return false;
  if (!Update.begin(size, U_FLASH)) return false;
  char hex[33]{};
  constexpr char digits[] = "0123456789abcdef";
  for (unsigned i = 0; i < 16; ++i) {
    hex[i * 2] = digits[md5[i] >> 4];
    hex[i * 2 + 1] = digits[md5[i] & 15];
  }
  running_ = true;
  if (!Update.setMD5(hex)) { abort(); return false; }
  expected_ = md5;
  size_ = size;
  written_ = 0;
  verified_ = false;
  digest_.begin();
  return true;
}

bool Esp8266OtaStorage::write(const std::uint8_t* data, std::size_t length) {
  if (!running_ || verified_ || !data || length == 0 || length > fanlink::ota::kBlockSize ||
      length > size_ - written_ || (written_ == 0 && data[0] != 0xE9)) return false;
  // The pinned Core's mutable API does not modify the caller's bytes. Still
  // isolate it in a fixed buffer so protocol/duplicate-cache data stays const.
  std::uint8_t copy[fanlink::ota::kBlockSize]{};
  std::copy_n(data, length, copy);
  if (Update.write(copy, length) != length) { abort(); return false; }
  digest_.add(data, static_cast<std::uint16_t>(length));
  written_ += length;
  return true;
}

bool Esp8266OtaStorage::verify() {
  if (verified_) return true;
  if (!running_ || written_ != size_ || Update.hasError() || !Update.isFinished())
    return false;
  digest_.calculate();
  std::array<std::uint8_t, 16> actual{};
  digest_.getBytes(actual.data());
  verified_ = actual == expected_;
  return verified_;
}

bool Esp8266OtaStorage::commit() {
  if (!running_ || !verified_) return false;
  // Sole path that can schedule eboot. Core validates its own MD5/header too.
  const bool success = Update.end(false);
  running_ = false;
  verified_ = false;
  return success;
}

void Esp8266OtaStorage::abort() {
  if (running_ && Update.isRunning()) {
    // Core 3.1.2 has no abort API. end(false) alone commits complete images!
    // setMD5 accepts any 32 characters; non-hex can never equal a real digest.
    // Incomplete images take end's premature reset; complete ones fail MD5.
    (void)Update.setMD5("XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX");
    (void)Update.end(false);
  }
  running_ = false;
  verified_ = false;
  size_ = written_ = 0;
}
}  // namespace fanlink::platform::esp8266
