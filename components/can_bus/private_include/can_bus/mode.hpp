#pragma once

#include "driver/twai.h"

namespace can_bus::internal {

// The vehicle build has no mode-selection input: absence of the explicit
// bench build guard always resolves to strict listen-only. The bench project
// supplies both guards through its component CMake configuration.
[[nodiscard]] twai_mode_t driver_mode() noexcept;

} // namespace can_bus::internal
