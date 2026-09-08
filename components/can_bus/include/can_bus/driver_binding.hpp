#pragma once

#include "driver/twai.h"

namespace can_bus::internal {

// The application-selected CAN component supplies this target binding. The
// shared receive engine calls it once while constructing the driver config;
// callers cannot select a mode at runtime, and each firmware application links
// exactly one binding implementation.
void configure_driver(twai_general_config_t &configuration) noexcept;

} // namespace can_bus::internal
