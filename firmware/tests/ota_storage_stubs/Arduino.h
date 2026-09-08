#pragma once
#include "../stubs/Arduino.h"
#include "ota_storage_fake.hpp"
inline std::uint32_t ESPClass::getFreeSketchSpace() const {
  ota_storage_fake::state.calls.emplace_back("space");
  return ota_storage_fake::state.freeSpace;
}
inline void ESPClass::restart() {
  ota_storage_fake::state.calls.emplace_back("restart");
}
inline ESPClass ESP;
