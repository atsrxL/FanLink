#pragma once

#include <cstdint>

#include "fanlink/protocol.hpp"

namespace fanlink {

// Used by TxController::runScheduledSlot(). This helper never sleeps and does
// not advance poll/sequence itself. The first call (also after reset) is due.
// Late work starts one slot at the current time, never a burst of catch-up polls.
// Unsigned elapsed time is wrap-safe if serviced at least once per micros wrap.
class CooperativePollCadence {
 public:
  explicit CooperativePollCadence(std::uint32_t periodUs = kNormalSlotPeriodUs)
      : periodUs_(periodUs) {}

  bool take(std::uint32_t nowUs) {
    if (started_ && static_cast<std::uint32_t>(nowUs - lastStartUs_) < periodUs_) {
      return false;
    }
    started_ = true;
    lastStartUs_ = nowUs;
    return true;
  }

  // Maintenance owns RF independently. Reset when normal polling resumes.
  void reset() { started_ = false; }

 private:
  std::uint32_t periodUs_;
  std::uint32_t lastStartUs_{0};
  bool started_{false};
};

}  // namespace fanlink
