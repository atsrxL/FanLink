#pragma once

#include <cstdint>

inline int startWaveformClockCycles(uint8_t,
                                    uint32_t,
                                    uint32_t,
                                    uint32_t = 0,
                                    int8_t = -1,
                                    uint32_t = 0,
                                    bool = false) {
  return 1;
}
inline int stopWaveform(uint8_t) { return 1; }
