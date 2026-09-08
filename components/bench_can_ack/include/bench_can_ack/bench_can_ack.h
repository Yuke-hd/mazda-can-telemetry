#pragma once

#include <cstdint>

namespace bench_can_ack {

struct CanPins {
  std::int8_t tx;
  std::int8_t rx;
};

// These pins belong to the isolated LILYGO/TTGO T-CAN485 bench target only.
// They are deliberately not part of the vehicle board capability record.
inline constexpr CanPins kCanPins{27, 26};

// The component is selected by the isolated bench application. It exposes
// the shared receive API through can_bus but never a data-frame TX operation.
inline constexpr char kTargetName[] = "tcan485_bench_ack_only";

} // namespace bench_can_ack
