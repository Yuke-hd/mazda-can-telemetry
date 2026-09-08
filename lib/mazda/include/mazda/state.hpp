#pragma once

#include <optional>

#include "mazda/freshness.hpp"
#include "mazda/types.hpp"
#include "vehicle_core/signal.hpp"

namespace mazda {

struct VehicleState {
  vehicle_core::MonotonicTimestamp timestamp_us{0};
  vehicle_core::Signal<float> speed_kph{vehicle_core::SignalUnit::KilometresPerHour};
  vehicle_core::Signal<float> engine_rpm{vehicle_core::SignalUnit::RevolutionsPerMinute};
  vehicle_core::Signal<SelectorPosition> selector_position{};
  vehicle_core::Signal<ActualGear> actual_gear{};
  vehicle_core::Signal<TurnState> turn_state{vehicle_core::SignalUnit::None,
                                             kTurnFreshnessTimeoutUs};
  vehicle_core::Signal<bool> hazard_request{vehicle_core::SignalUnit::Boolean,
                                            kRequestFreshnessTimeoutUs};
  vehicle_core::Signal<bool> left_turn_request{vehicle_core::SignalUnit::Boolean,
                                               kRequestFreshnessTimeoutUs};
  vehicle_core::Signal<bool> right_turn_request{vehicle_core::SignalUnit::Boolean,
                                                kRequestFreshnessTimeoutUs};
  vehicle_core::Signal<bool> liftgate_open{vehicle_core::SignalUnit::Boolean};
  vehicle_core::Signal<bool> rear_right_door_open{vehicle_core::SignalUnit::Boolean};
  vehicle_core::Signal<bool> rear_left_door_open{vehicle_core::SignalUnit::Boolean};
  vehicle_core::Signal<bool> front_left_door_open_rhd{vehicle_core::SignalUnit::Boolean};
  vehicle_core::Signal<bool> front_right_door_open_rhd{vehicle_core::SignalUnit::Boolean};
  vehicle_core::Signal<bool> doors_unlocked{vehicle_core::SignalUnit::Boolean};
  vehicle_core::Signal<bool> left_indicator_lamp{vehicle_core::SignalUnit::Boolean};
  vehicle_core::Signal<bool> right_indicator_lamp{vehicle_core::SignalUnit::Boolean};
  vehicle_core::Signal<bool> wiper_low{vehicle_core::SignalUnit::Boolean};
  vehicle_core::Signal<FrontWiperPosition> front_wiper{};

  // Apply a semantic turn state and return an edge only when the state
  // changed. The first known state has Unknown as its previous state.
  std::optional<TurnEdgeEvent> update_turn(TurnState state,
                                           vehicle_core::MonotonicTimestamp timestamp_us) noexcept;

  // Unknown and stale turn values are never actionable.
  [[nodiscard]] TurnState effective_turn_state() const noexcept {
    return turn_state.is_valid() ? turn_state.value : TurnState::Unknown;
  }

  [[nodiscard]] VehicleState snapshot(vehicle_core::MonotonicTimestamp now_us) const noexcept;
  [[nodiscard]] VehicleState snapshot(vehicle_core::MonotonicTimestamp now_us,
                                      const VehicleFreshnessPolicy &policy) const noexcept;

  void refresh(vehicle_core::MonotonicTimestamp now_us) noexcept;
  void apply_freshness_policy(const VehicleFreshnessPolicy &policy) noexcept;
};

class SnapshotProvider {
public:
  virtual ~SnapshotProvider() = default;
  [[nodiscard]] virtual VehicleState snapshot() const noexcept = 0;
};

// A small state owner for deterministic integrations. It owns one state by
// value, performs no allocation, and exposes snapshots only by value.
class VehicleStateStore final : public SnapshotProvider {
public:
  explicit VehicleStateStore(vehicle_core::MonotonicClock &clock,
                             VehicleFreshnessPolicy policy = {}) noexcept;

  [[nodiscard]] VehicleState &mutable_state() noexcept { return state_; }
  [[nodiscard]] const VehicleState &state() const noexcept { return state_; }
  [[nodiscard]] VehicleState snapshot() const noexcept override;

private:
  vehicle_core::MonotonicClock *clock_;
  VehicleFreshnessPolicy policy_;
  VehicleState state_{};
};

} // namespace mazda
