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

std::optional<TurnEdgeEvent>
VehicleState::update_turn(TurnState state, MonotonicTimestamp timestamp) noexcept {
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

void VehicleState::refresh(MonotonicTimestamp now,
                           Microseconds freshness_timeout) noexcept {
  speed_kph.refresh(now, freshness_timeout);
  engine_rpm.refresh(now, freshness_timeout);
  gear.refresh(now, freshness_timeout);
  turn_state.refresh(now, freshness_timeout);
  hazard_request.refresh(now, freshness_timeout);
  left_turn_request.refresh(now, freshness_timeout);
  right_turn_request.refresh(now, freshness_timeout);
}

VehicleState VehicleState::snapshot(MonotonicTimestamp now,
                                    Microseconds freshness_timeout) const noexcept {
  VehicleState result = *this;
  result.refresh(now, freshness_timeout);
  result.timestamp_us = now;
  return result;
}

VehicleStateStore::VehicleStateStore(MonotonicClock& clock,
                                     Microseconds freshness_timeout) noexcept
    : clock_(&clock), freshness_timeout_us_(freshness_timeout) {}

VehicleState VehicleStateStore::snapshot() const noexcept {
  return state_.snapshot(clock_->now(), freshness_timeout_us_);
}

bool library_is_available() noexcept { return true; }

} // namespace vehicle_core
