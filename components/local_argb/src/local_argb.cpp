#include "local_argb/local_argb.h"

namespace local_argb {

SemanticSnapshot from_vehicle_state(const vehicle_core::VehicleState &state,
                                    const SemanticHealth health) noexcept {
  return SemanticSnapshot{state.turn_state.value, state.turn_state.status,
                          state.turn_state.last_update_us, health};
}

Rgb color_for(const SemanticSnapshot &snapshot,
              const vehicle_core::MonotonicTimestamp now_us) noexcept {
  if (snapshot.health != SemanticHealth::Online ||
      snapshot.turn_status != vehicle_core::SignalStatus::Valid ||
      now_us < snapshot.turn_last_update_us ||
      now_us - snapshot.turn_last_update_us > kFailOffTimeoutUs) {
    return kBlack;
  }
  switch (snapshot.turn) {
  case vehicle_core::TurnState::Left:
    return kLeftGreen;
  case vehicle_core::TurnState::Right:
    return kRightBlue;
  case vehicle_core::TurnState::Hazard:
    return kHazardAmber;
  case vehicle_core::TurnState::Unknown:
  case vehicle_core::TurnState::Off:
    return kBlack;
  }
  return kBlack;
}

bool Controller::start() noexcept {
  has_snapshot_ = false;
  has_last_written_ = false;
  faulted_ = false;
  return write_desired(kBlack);
}

bool Controller::apply(const SemanticSnapshot snapshot,
                       const vehicle_core::MonotonicTimestamp now_us) noexcept {
  snapshot_ = snapshot;
  has_snapshot_ = true;
  // A fresh semantic submission is a recovery boundary only after black was
  // successfully written. If the clear also failed, keep retrying black.
  if (!faulted_ || (has_last_written_ && last_written_ == kBlack)) {
    faulted_ = false;
  }
  return tick(now_us);
}

bool Controller::tick(const vehicle_core::MonotonicTimestamp now_us) noexcept {
  const Rgb desired = faulted_ || !has_snapshot_ ? kBlack : color_for(snapshot_, now_us);
  return write_desired(desired);
}

bool Controller::write_desired(const Rgb desired) noexcept {
  if (has_last_written_ && desired == last_written_) {
    return true;
  }
  if (sink_->write(desired)) {
    last_written_ = desired;
    has_last_written_ = true;
    return true;
  }

  faulted_ = true;
  has_last_written_ = false;
  if (desired != kBlack && sink_->write(kBlack)) {
    last_written_ = kBlack;
    has_last_written_ = true;
  }
  return false;
}

} // namespace local_argb
