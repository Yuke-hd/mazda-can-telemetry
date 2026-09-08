#include "mazda/state.hpp"

#include <algorithm>

namespace mazda {

std::optional<TurnEdgeEvent>
VehicleState::update_turn(const TurnState state,
                          const vehicle_core::MonotonicTimestamp timestamp) noexcept {
  // A mutable receive state can sit without a snapshot between packets. Make
  // freshness part of the edge decision so recovery is Unknown -> state even
  // when the raw value happens to match the last stored direction.
  turn_state.refresh(timestamp);
  const TurnState previous = effective_turn_state();
  if (!turn_state.update(state, timestamp)) {
    return std::nullopt;
  }
  timestamp_us = std::max(timestamp_us, timestamp);
  if (previous == state) {
    return std::nullopt;
  }
  return TurnEdgeEvent{TurnEventType::StateChanged, previous, state, timestamp};
}

void VehicleState::refresh(const vehicle_core::MonotonicTimestamp now) noexcept {
  speed_kph.refresh(now);
  engine_rpm.refresh(now);
  selector_position.refresh(now);
  actual_gear.refresh(now);
  liftgate_open.refresh(now);
  rear_right_door_open.refresh(now);
  rear_left_door_open.refresh(now);
  front_left_door_open_rhd.refresh(now);
  front_right_door_open_rhd.refresh(now);
  doors_unlocked.refresh(now);
  left_indicator_lamp.refresh(now);
  right_indicator_lamp.refresh(now);
  wiper_low.refresh(now);
  front_wiper.refresh(now);
  turn_state.refresh(now);
  hazard_request.refresh(now);
  left_turn_request.refresh(now);
  right_turn_request.refresh(now);
}

void VehicleState::apply_freshness_policy(const VehicleFreshnessPolicy &policy) noexcept {
  speed_kph.set_freshness_timeout(policy.speed_kph_timeout_us);
  engine_rpm.set_freshness_timeout(policy.engine_rpm_timeout_us);
  selector_position.set_freshness_timeout(policy.selector_position_timeout_us);
  actual_gear.set_freshness_timeout(policy.actual_gear_timeout_us);
  liftgate_open.set_freshness_timeout(policy.liftgate_open_timeout_us);
  rear_right_door_open.set_freshness_timeout(policy.rear_right_door_open_timeout_us);
  rear_left_door_open.set_freshness_timeout(policy.rear_left_door_open_timeout_us);
  front_left_door_open_rhd.set_freshness_timeout(policy.front_left_door_open_rhd_timeout_us);
  front_right_door_open_rhd.set_freshness_timeout(policy.front_right_door_open_rhd_timeout_us);
  doors_unlocked.set_freshness_timeout(policy.doors_unlocked_timeout_us);
  left_indicator_lamp.set_freshness_timeout(policy.left_indicator_lamp_timeout_us);
  right_indicator_lamp.set_freshness_timeout(policy.right_indicator_lamp_timeout_us);
  wiper_low.set_freshness_timeout(policy.wiper_low_timeout_us);
  front_wiper.set_freshness_timeout(policy.front_wiper_timeout_us);
  turn_state.set_freshness_timeout(policy.turn_state_timeout_us);
  hazard_request.set_freshness_timeout(policy.hazard_request_timeout_us);
  left_turn_request.set_freshness_timeout(policy.left_turn_request_timeout_us);
  right_turn_request.set_freshness_timeout(policy.right_turn_request_timeout_us);
}

VehicleState VehicleState::snapshot(const vehicle_core::MonotonicTimestamp now) const noexcept {
  VehicleState result = *this;
  result.refresh(now);
  result.timestamp_us = now;
  return result;
}

VehicleState VehicleState::snapshot(const vehicle_core::MonotonicTimestamp now,
                                    const VehicleFreshnessPolicy &policy) const noexcept {
  VehicleState result = *this;
  result.apply_freshness_policy(policy);
  return result.snapshot(now);
}

VehicleStateStore::VehicleStateStore(vehicle_core::MonotonicClock &clock,
                                     VehicleFreshnessPolicy policy) noexcept
    : clock_(&clock), policy_(policy) {}

VehicleState VehicleStateStore::snapshot() const noexcept {
  return state_.snapshot(clock_->now(), policy_);
}

} // namespace mazda
