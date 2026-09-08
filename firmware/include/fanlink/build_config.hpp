#pragma once

#include <cstdint>

// All values in this file are starter defaults. Verify them against the exact
// transmitter/receiver hardware before flashing.

#ifndef FANLINK_NETWORK_ID
#define FANLINK_NETWORK_ID 0x4B4E4C46UL
#endif

#ifndef FANLINK_NODE_ID
#define FANLINK_NODE_ID 1
#endif

#ifndef FANLINK_HOST_BAUD
#define FANLINK_HOST_BAUD 115200UL
#endif

#ifndef FANLINK_HOST_TIMEOUT_MS
#define FANLINK_HOST_TIMEOUT_MS 500UL
#endif

#ifndef FANLINK_RX_LINK_TIMEOUT_MS
#define FANLINK_RX_LINK_TIMEOUT_MS 500UL
#endif

#ifndef FANLINK_SLOT_PERIOD_US
#define FANLINK_SLOT_PERIOD_US 125000UL
#endif

#ifndef FANLINK_REPLY_DELAY_US
#define FANLINK_REPLY_DELAY_US 1200UL
#endif

#ifndef FANLINK_REPLY_TIMEOUT_MS
#define FANLINK_REPLY_TIMEOUT_MS 5UL
#endif

#ifndef FANLINK_RX_RECEIVE_TIMEOUT_MS
#define FANLINK_RX_RECEIVE_TIMEOUT_MS 25UL
#endif

#ifndef FANLINK_STATUS_PERIOD_MS
#define FANLINK_STATUS_PERIOD_MS 250UL
#endif

#ifndef FANLINK_PWM_HZ
#define FANLINK_PWM_HZ 25000UL
#endif

#ifndef FANLINK_TACH_PULSES_PER_REV
#define FANLINK_TACH_PULSES_PER_REV 2U
#endif

#ifndef FANLINK_TACH_TIMEOUT_US
#define FANLINK_TACH_TIMEOUT_US 1000000UL
#endif

#ifndef FANLINK_TACH_MIN_EDGE_US
#define FANLINK_TACH_MIN_EDGE_US 250UL
#endif

#ifndef FANLINK_TACH_MAX_RPM
#define FANLINK_TACH_MAX_RPM 30000UL
#endif

#ifndef FANLINK_STALL_DUTY_THRESHOLD
#define FANLINK_STALL_DUTY_THRESHOLD 64U
#endif

#ifndef FANLINK_STALL_GRACE_MS
#define FANLINK_STALL_GRACE_MS 1500UL
#endif

// SX1280 starter RF values. Fixed frequency is deliberate for bring-up.
#ifndef FANLINK_RADIO_FREQUENCY_MHZ
#define FANLINK_RADIO_FREQUENCY_MHZ 2405.0F
#endif

#ifndef FANLINK_RADIO_BITRATE_KBPS
#define FANLINK_RADIO_BITRATE_KBPS 650.0F
#endif

#ifndef FANLINK_RADIO_CODING_RATE
#define FANLINK_RADIO_CODING_RATE 3U
#endif

#ifndef FANLINK_RADIO_OUTPUT_POWER_DBM
#define FANLINK_RADIO_OUTPUT_POWER_DBM -18
#endif

#ifndef FANLINK_RADIO_PREAMBLE_BITS
#define FANLINK_RADIO_PREAMBLE_BITS 16U
#endif

#ifndef FANLINK_RADIO_SPI_HZ
#define FANLINK_RADIO_SPI_HZ 2000000UL
#endif

#ifndef FANLINK_RADIO_BUSY_TIMEOUT_MS
#define FANLINK_RADIO_BUSY_TIMEOUT_MS 25UL
#endif

// Shared SX1280 pins for the reviewed BETAFPV Lite (both roles) upstream
// targets. PlatformIO supplies the board-specific RF-switch/regulator values.
// These values still require PCB-revision and bench verification before flash.
#ifndef FANLINK_RADIO_NSS_PIN
#define FANLINK_RADIO_NSS_PIN 15
#endif

#ifndef FANLINK_RADIO_DIO1_PIN
#define FANLINK_RADIO_DIO1_PIN 4
#endif

#ifndef FANLINK_RADIO_RST_PIN
#define FANLINK_RADIO_RST_PIN 2
#endif

#ifndef FANLINK_RADIO_BUSY_PIN
#define FANLINK_RADIO_BUSY_PIN 5
#endif

#ifndef FANLINK_RADIO_RXEN_PIN
#define FANLINK_RADIO_RXEN_PIN -1
#endif

#ifndef FANLINK_RADIO_TXEN_PIN
#define FANLINK_RADIO_TXEN_PIN -1
#endif

#ifndef FANLINK_RADIO_USE_DCDC
#define FANLINK_RADIO_USE_DCDC 0
#endif

#ifndef FANLINK_FAN_PWM_PIN
#define FANLINK_FAN_PWM_PIN 1
#endif

#ifndef FANLINK_TACH_PIN
#define FANLINK_TACH_PIN 3
#endif

#ifndef FANLINK_STATUS_LED_PIN
#define FANLINK_STATUS_LED_PIN -1
#endif

#ifndef FANLINK_STATUS_LED_ACTIVE_HIGH
#define FANLINK_STATUS_LED_ACTIVE_HIGH 1
#endif

// 1 means an active-high GPIO drives an external N-MOS/NPN on. Because that
// external stage pulls the fan PWM wire low, logical fan duty is inverted.
#ifndef FANLINK_GATE_ACTIVE_HIGH
#define FANLINK_GATE_ACTIVE_HIGH 1
#endif

namespace fanlink::build {

inline constexpr std::uint32_t kNetworkId = FANLINK_NETWORK_ID;
inline constexpr std::uint8_t kNodeId = FANLINK_NODE_ID;
inline constexpr std::uint32_t kHostBaud = FANLINK_HOST_BAUD;
inline constexpr std::uint32_t kHostTimeoutMs = FANLINK_HOST_TIMEOUT_MS;
inline constexpr std::uint32_t kRxLinkTimeoutMs = FANLINK_RX_LINK_TIMEOUT_MS;
inline constexpr std::uint32_t kSlotPeriodUs = FANLINK_SLOT_PERIOD_US;
inline constexpr std::uint32_t kReplyDelayUs = FANLINK_REPLY_DELAY_US;
inline constexpr std::uint32_t kReplyTimeoutMs = FANLINK_REPLY_TIMEOUT_MS;
inline constexpr std::uint32_t kRxReceiveTimeoutMs = FANLINK_RX_RECEIVE_TIMEOUT_MS;
inline constexpr std::uint32_t kStatusPeriodMs = FANLINK_STATUS_PERIOD_MS;
inline constexpr std::uint32_t kPwmHz = FANLINK_PWM_HZ;
inline constexpr std::uint8_t kTachPulsesPerRev = FANLINK_TACH_PULSES_PER_REV;
inline constexpr std::uint32_t kTachTimeoutUs = FANLINK_TACH_TIMEOUT_US;
inline constexpr std::uint32_t kTachMinEdgeUs = FANLINK_TACH_MIN_EDGE_US;
inline constexpr std::uint32_t kTachMaxRpm = FANLINK_TACH_MAX_RPM;
inline constexpr std::uint8_t kStallDutyThreshold = FANLINK_STALL_DUTY_THRESHOLD;
inline constexpr std::uint32_t kStallGraceMs = FANLINK_STALL_GRACE_MS;
inline constexpr float kRadioFrequencyMhz = FANLINK_RADIO_FREQUENCY_MHZ;
inline constexpr float kRadioBitrateKbps = FANLINK_RADIO_BITRATE_KBPS;
inline constexpr std::uint8_t kRadioCodingRate = FANLINK_RADIO_CODING_RATE;
inline constexpr std::int8_t kRadioOutputPowerDbm = FANLINK_RADIO_OUTPUT_POWER_DBM;
inline constexpr std::uint16_t kRadioPreambleBits = FANLINK_RADIO_PREAMBLE_BITS;
inline constexpr std::uint32_t kRadioSpiHz = FANLINK_RADIO_SPI_HZ;
inline constexpr std::uint32_t kRadioBusyTimeoutMs = FANLINK_RADIO_BUSY_TIMEOUT_MS;
inline constexpr int kRadioNssPin = FANLINK_RADIO_NSS_PIN;
inline constexpr int kRadioDio1Pin = FANLINK_RADIO_DIO1_PIN;
inline constexpr int kRadioRstPin = FANLINK_RADIO_RST_PIN;
inline constexpr int kRadioBusyPin = FANLINK_RADIO_BUSY_PIN;
inline constexpr int kRadioRxEnPin = FANLINK_RADIO_RXEN_PIN;
inline constexpr int kRadioTxEnPin = FANLINK_RADIO_TXEN_PIN;
inline constexpr bool kRadioUseDcdc = FANLINK_RADIO_USE_DCDC != 0;
inline constexpr int kFanPwmPin = FANLINK_FAN_PWM_PIN;
inline constexpr int kTachPin = FANLINK_TACH_PIN;
inline constexpr bool kGateActiveHigh = FANLINK_GATE_ACTIVE_HIGH != 0;
inline constexpr int kStatusLedPin = FANLINK_STATUS_LED_PIN;
inline constexpr bool kStatusLedActiveHigh = FANLINK_STATUS_LED_ACTIVE_HIGH != 0;

static_assert(kNodeId >= 1 && kNodeId <= 8, "FANLINK_NODE_ID must be 1..8");
static_assert(kTachPulsesPerRev > 0, "Tach pulses per revolution must be non-zero");
static_assert(kRadioBusyTimeoutMs > 0U, "Radio BUSY timeout must be non-zero");
static_assert(kRadioBusyTimeoutMs < kRxLinkTimeoutMs,
              "Radio BUSY timeout must remain below the RX link watchdog");
static_assert(kStatusLedPin == -1 || (kStatusLedPin >= 0 && kStatusLedPin <= 16),
              "ESP8285 status LED pin must be -1 or GPIO0..16");
static_assert(kStatusLedPin < 0 || kStatusLedPin != kFanPwmPin,
              "Status LED must not share the fan PWM GPIO");
static_assert(kStatusLedPin < 0 || kStatusLedPin != kTachPin,
              "Status LED must not share the Tach GPIO");
static_assert(kStatusLedPin < 0 || kStatusLedPin != kRadioNssPin,
              "Status LED must not share the radio NSS GPIO");
static_assert(kStatusLedPin < 0 || kStatusLedPin != kRadioDio1Pin,
              "Status LED must not share the radio DIO1 GPIO");
static_assert(kStatusLedPin < 0 || kStatusLedPin != kRadioRstPin,
              "Status LED must not share the radio reset GPIO");
static_assert(kStatusLedPin < 0 || kStatusLedPin != kRadioBusyPin,
              "Status LED must not share the radio BUSY GPIO");
static_assert(kStatusLedPin < 0 || kStatusLedPin != kRadioRxEnPin,
              "Status LED must not share the radio RXEN GPIO");
static_assert(kStatusLedPin < 0 || kStatusLedPin != kRadioTxEnPin,
              "Status LED must not share the radio TXEN GPIO");

}  // namespace fanlink::build
