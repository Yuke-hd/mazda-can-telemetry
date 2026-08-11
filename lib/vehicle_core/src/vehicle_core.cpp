#include "vehicle_core/vehicle_core.hpp"

#include <algorithm>

namespace vehicle_core {

bool RawCanFrame::is_valid() const noexcept {
  if (dlc > kCanClassicPayloadBytes) {
    return false;
  }
  if (identifier_format == CanIdentifierFormat::Standard) {
    return identifier <= 0x7ffU;
  }
  return identifier <= 0x1fffffffU;
}

std::optional<TurnEdgeEvent> VehicleState::update_turn(TurnState state,
                                                       MonotonicTimestamp timestamp) noexcept {
  const TurnState previous = turn_state.value;
  if (!turn_state.update(state, timestamp)) {
    return std::nullopt;
  }
  timestamp_us = std::max(timestamp_us, timestamp);
  if (previous == state) {
    return std::nullopt;
  }
  return TurnEdgeEvent{TurnEventType::StateChanged, previous, state, timestamp};
}

void VehicleState::refresh(MonotonicTimestamp now) noexcept {
  speed_kph.refresh(now);
  engine_rpm.refresh(now);
  selector_position.refresh(now);
  actual_gear.refresh(now);
  turn_state.refresh(now);
  hazard_request.refresh(now);
  left_turn_request.refresh(now);
  right_turn_request.refresh(now);
}

VehicleState VehicleState::snapshot(MonotonicTimestamp now) const noexcept {
  VehicleState result = *this;
  result.refresh(now);
  result.timestamp_us = now;
  return result;
}

VehicleStateStore::VehicleStateStore(MonotonicClock &clock) noexcept : clock_(&clock) {}

VehicleState VehicleStateStore::snapshot() const noexcept { return state_.snapshot(clock_->now()); }

bool library_is_available() noexcept { return true; }

} // namespace vehicle_core
