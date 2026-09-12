#include "local_argb/local_argb.h"

#include "mazda/state.hpp"
#include "mazda/types.hpp"

namespace local_argb {

SemanticSnapshot from_vehicle_state(const mazda::VehicleState &state,
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
  case mazda::TurnState::Left:
    return kLeftGreen;
  case mazda::TurnState::Right:
    return kRightBlue;
  case mazda::TurnState::Hazard:
    return kHazardAmber;
  case mazda::TurnState::Unknown:
  case mazda::TurnState::Off:
    return kBlack;
  }
  return kBlack;
}

namespace {

LightingCommand command_from_snapshot(const SemanticSnapshot &snapshot) noexcept {
  LightingCommand command{};
  command.color = color_for(snapshot, snapshot.turn_last_update_us);
  command.valid_until_us = snapshot.turn_last_update_us + kFailOffTimeoutUs;
  command.actionable = command.color != kBlack && snapshot.health == SemanticHealth::Online &&
                       snapshot.turn_status == vehicle_core::SignalStatus::Valid;
  if (!command.actionable) {
    command.color = kBlack;
  }
  return command;
}

} // namespace

bool Controller::start() noexcept {
  has_command_ = false;
  has_snapshot_ = false;
  has_last_written_ = false;
  faulted_ = false;
  return write_desired(kBlack);
}

bool Controller::apply(const LightingCommand command,
                       const vehicle_core::MonotonicTimestamp now_us) noexcept {
  command_ = command;
  has_command_ = true;
  // A fresh command is a recovery boundary only after black was successfully
  // written. If the clear also failed, keep retrying black.
  if (!faulted_ || (has_last_written_ && last_written_ == kBlack)) {
    faulted_ = false;
  }
  (void)now_us;
  return tick(now_us);
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
  return apply(command_from_snapshot(snapshot), now_us);
}

bool Controller::tick(const vehicle_core::MonotonicTimestamp now_us) noexcept {
  Rgb desired = kBlack;
  if (!faulted_) {
    if (has_command_) {
      const bool expired = now_us > command_.valid_until_us;
      if (command_.actionable && !expired) {
        desired = command_.color;
      }
    } else if (has_snapshot_) {
      desired = color_for(snapshot_, now_us);
    }
  }
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

bool PublicationPolicy::should_publish(const SemanticSnapshot snapshot,
                                       const vehicle_core::MonotonicTimestamp now_us) noexcept {
  const bool changed = !has_published_ || snapshot.turn != last_published_.turn ||
                       snapshot.turn_status != last_published_.turn_status ||
                       snapshot.health != last_published_.health;
  const bool heartbeat = has_published_ && (now_us < last_publish_us_ ||
                                            now_us - last_publish_us_ >= kPublishHeartbeatUs);
  if (!changed && !heartbeat) {
    return false;
  }
  last_published_ = snapshot;
  last_publish_us_ = now_us;
  has_published_ = true;
  return true;
}

} // namespace local_argb
