#pragma once

#include "can_bus/can_bus.h"

namespace can_bus::internal {

[[nodiscard]] constexpr std::uint64_t counter_delta(const std::uint32_t current,
                                                    const std::uint32_t previous) noexcept {
  // Unsigned subtraction is defined modulo 2^32, preserving deltas across a
  // cumulative driver's uint32_t counter wrap.
  return static_cast<std::uint64_t>(current - previous);
}

[[nodiscard]] constexpr bool is_configuration_valid(const Configuration &configuration) noexcept {
  switch (configuration.bitrate_bps) {
  case 125'000:
  case 250'000:
  case 500'000:
  case 1'000'000:
    return true;
  default:
    return false;
  }
}

} // namespace can_bus::internal
