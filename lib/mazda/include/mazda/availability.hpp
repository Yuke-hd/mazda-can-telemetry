#pragma once

// Mazda-facing aliases for the frozen, value-copy availability contract.
// Definitions remain in vehicle_core so generic consumers can reason about
// data availability without depending on Mazda decoders or state storage.
#include "vehicle_core/telemetry_contracts.hpp"

namespace mazda {

using vehicle_core::Availability;
using vehicle_core::Notification;
using vehicle_core::Reading;
using vehicle_core::ValidationStatus;

} // namespace mazda
