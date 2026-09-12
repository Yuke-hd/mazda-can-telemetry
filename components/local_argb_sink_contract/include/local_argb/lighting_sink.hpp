#pragma once

#include <cstdint>

#include "vehicle_core/time.hpp"

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

// Adapt an implementation-owned generic command (for example, the output
// of vehicle_lighting_policy) without exposing either module's private class
// to the other. The source is required to provide RGB fields, an absolute
// valid-until timestamp and an actionable bit; no vehicle or decoder type can
// cross this boundary.
template <typename GenericCommand>
[[nodiscard]] inline LightingCommand adapt_command(const GenericCommand &source) noexcept {
  return LightingCommand{{source.color.red, source.color.green, source.color.blue},
                         source.valid_until_us,
                         source.actionable};
}

// The service obtains this implementation-only handoff explicitly. Ordinary
// facade consumers only see the generic public renderer values, not the sink
// operation or queue ownership.
[[nodiscard]] LightingSink &sink() noexcept;

} // namespace local_argb::internal
