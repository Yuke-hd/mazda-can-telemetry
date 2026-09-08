#pragma once

#include <cstdint>

namespace vehicle_core {

// Timestamps crossing the portable boundary are monotonic microseconds.
using MonotonicTimestamp = std::uint64_t;
using Microseconds = std::uint64_t;

// Production adapts its monotonic timer to this interface; host tests provide
// a deterministic implementation. The clock is borrowed and never owned.
class MonotonicClock {
public:
  virtual ~MonotonicClock() = default;
  [[nodiscard]] virtual MonotonicTimestamp now() const noexcept = 0;
};

} // namespace vehicle_core
