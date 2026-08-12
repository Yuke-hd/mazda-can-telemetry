#pragma once

#include "can_bus/can_bus.h"

namespace can_bus::internal {

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
