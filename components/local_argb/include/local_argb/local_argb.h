#pragma once

#include <cstdint>

#include "vehicle_core/time.hpp"

namespace local_argb {

// Generic renderer values. No vehicle, transport, board, or RTOS
// type crosses the ordinary local_argb include boundary.
struct Rgb {
  std::uint8_t red{0};
  std::uint8_t green{0};
  std::uint8_t blue{0};
};

constexpr bool operator==(const Rgb &left, const Rgb &right) noexcept {
  return left.red == right.red && left.green == right.green && left.blue == right.blue;
}
constexpr bool operator!=(const Rgb &left, const Rgb &right) noexcept { return !(left == right); }

inline constexpr std::uint8_t kBrightnessCeiling = 16;
inline constexpr Rgb kBlack{};

// These timing values describe renderer supervision, not vehicle freshness.
// Freshness deadlines are carried by the private LightingCommand handoff.
inline constexpr vehicle_core::Microseconds kDriverHangRestartUs = 100'000;
inline constexpr vehicle_core::Microseconds kWorkerStallRestartUs = 100'000;
inline constexpr vehicle_core::Microseconds kSupervisorPollUs = 10'000;
inline constexpr vehicle_core::Microseconds kDriverRestartRequestBoundUs =
    kDriverHangRestartUs + kSupervisorPollUs;
inline constexpr vehicle_core::Microseconds kWorkerRestartRequestBoundUs =
    kWorkerStallRestartUs + kSupervisorPollUs;

class PixelSink {
public:
  virtual ~PixelSink() = default;
  virtual bool write(Rgb color) noexcept = 0;
};

// ESP-IDF runtime. start() sends an explicit black RMT frame before returning.
bool start() noexcept;
void fail_off() noexcept;

} // namespace local_argb
