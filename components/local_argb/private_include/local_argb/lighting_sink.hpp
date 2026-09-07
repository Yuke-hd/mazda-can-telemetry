#pragma once

#include <cstdint>

#include "vehicle_core/vehicle_core.hpp"

namespace local_argb::internal {

struct LightingRgb {
  std::uint8_t red{0};
  std::uint8_t green{0};
  std::uint8_t blue{0};
};

struct LightingCommand {
  LightingRgb color{};
  vehicle_core::MonotonicTimestamp valid_until_us{0};
  bool actionable{false};
};

// Private, value-only sink between the portable lighting policy and the
// renderer. Mazda enums, decoder health, and driver handles do not cross it.
class LightingSink {
public:
  virtual ~LightingSink() = default;
  virtual bool publish(const LightingCommand &command) noexcept = 0;
};

} // namespace local_argb::internal
