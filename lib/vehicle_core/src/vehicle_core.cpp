#include "vehicle_core/vehicle_core.hpp"

namespace vehicle_core {

bool is_fresh(const Signal<float> &signal, const std::uint64_t now_us,
              const std::uint64_t timeout_us) {
  if (signal.validity != Validity::Valid || now_us < signal.last_update_us) {
    return false;
  }
  return now_us - signal.last_update_us <= timeout_us;
}

VehicleState make_unknown_state(const std::uint64_t timestamp_us) {
  VehicleState state{};
  state.timestamp_us = timestamp_us;
  state.speed_kph.validity = Validity::Unknown;
  state.engine_rpm.validity = Validity::Unknown;
  state.gear.validity = Validity::Unknown;
  state.turn.validity = Validity::Unknown;
  state.turn.value = TurnState::Unknown;
  return state;
}

} // namespace vehicle_core
