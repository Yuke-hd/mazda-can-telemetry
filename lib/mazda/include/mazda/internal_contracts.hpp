#pragma once

#include "mazda/state.hpp"
#include "vehicle_core/decoder_contracts.hpp"
#include "vehicle_core/frame.hpp"
#include "vehicle_core/telemetry_contracts.hpp"

namespace mazda {

// Internal handoff contracts keep raw acquisition and decoder details out of
// the application facade while allowing later modules to be implemented
// independently.
struct DecoderInput {
  vehicle_core::RawCanFrame frame{};
};

struct DecoderOutput {
  vehicle_core::DecoderObservation observation{};
  vehicle_core::HealthObservation health{};
};

struct ServiceUpdate {
  vehicle_core::MonotonicTimestamp now_us{0};
  DecoderOutput decoded{};
};

struct LightingUpdate {
  TurnState turn{TurnState::Unknown};
  vehicle_core::Availability availability{vehicle_core::Availability::NoData};
  vehicle_core::MonotonicTimestamp valid_until_us{0};
};

} // namespace mazda
