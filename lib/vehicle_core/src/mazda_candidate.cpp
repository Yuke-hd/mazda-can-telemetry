#include "vehicle_core/vehicle_core.hpp"

namespace vehicle_core::mazda_candidate {
namespace {

bool candidate_frame(const RawCanFrame &frame, const std::uint32_t identifier) noexcept {
  return frame.is_valid() && !frame.is_extended() && !frame.remote_request &&
         frame.identifier == identifier;
}

std::uint16_t big_endian_u16(const std::array<std::uint8_t, kCanClassicPayloadBytes> &data,
                             const std::size_t offset) noexcept {
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[offset]) << 8U) |
                                    data[offset + 1]);
}

SelectorPosition selector_from_raw(const std::uint8_t raw) noexcept {
  switch (raw) {
  case 1:
    return SelectorPosition::Park;
  case 2:
    return SelectorPosition::Reverse;
  case 3:
    return SelectorPosition::Neutral;
  case 4:
    return SelectorPosition::Drive;
  default:
    return SelectorPosition::Unknown;
  }
}

ActualGear actual_gear_from_raw(const std::uint8_t raw) noexcept {
  switch (raw) {
  case 0:
    return ActualGear::ParkOrNeutral;
  case 1:
    return ActualGear::First;
  case 2:
    return ActualGear::Second;
  case 3:
    return ActualGear::Third;
  case 4:
    return ActualGear::Fourth;
  case 5:
    return ActualGear::Fifth;
  case 6:
    return ActualGear::Sixth;
  case 14:
    return ActualGear::Reverse;
  default:
    return ActualGear::Unknown;
  }
}

FrontWiperPosition front_wiper_from_raw(const std::uint8_t raw) noexcept {
  switch (raw) {
  case 0:
    return FrontWiperPosition::Off;
  case 1:
    return FrontWiperPosition::On;
  case 2:
    return FrontWiperPosition::High;
  case 3:
    return FrontWiperPosition::Intermittent;
  default:
    return FrontWiperPosition::Unknown;
  }
}

TurnState normalize_turn(const bool hazard, const bool left, const bool right) noexcept {
  if (hazard)
    return TurnState::Hazard;
  if (left && right)
    return TurnState::Unknown;
  if (left)
    return TurnState::Left;
  if (right)
    return TurnState::Right;
  return TurnState::Off;
}

} // namespace

DecodeStatus decode_engine_data(const RawCanFrame &frame, VehicleState &state) noexcept {
  if (!candidate_frame(frame, kEngineDataId))
    return DecodeStatus::Ignored;
  if (frame.dlc != kCandidateDlc)
    return DecodeStatus::Invalid;

  const auto rpm_raw = big_endian_u16(frame.data, 0);
  const auto speed_raw = big_endian_u16(frame.data, 2);
  // The confirmed DBC declares EngineRPM in [0, 8500]. SPEED remains the
  // existing out-of-scope candidate; it has no invalid sentinel and its
  // 16-bit representation is already non-negative.
  if (rpm_raw > 34000U)
    return DecodeStatus::Invalid;

  const bool rpm_updated =
      state.engine_rpm.update(static_cast<float>(rpm_raw) * 0.25F, frame.timestamp_us);
  const bool speed_updated =
      state.speed_kph.update(static_cast<float>(speed_raw) * 0.01F, frame.timestamp_us);
  if (rpm_updated || speed_updated) {
    if (frame.timestamp_us > state.timestamp_us)
      state.timestamp_us = frame.timestamp_us;
    return DecodeStatus::Updated;
  }
  return DecodeStatus::Invalid;
}

DecodeStatus decode_gear(const RawCanFrame &frame, VehicleState &state) noexcept {
  if (!candidate_frame(frame, kGearId))
    return DecodeStatus::Ignored;
  if (frame.dlc != kCandidateDlc)
    return DecodeStatus::Invalid;

  const auto selector_raw = static_cast<std::uint8_t>(frame.data[0] & 0x07U);
  const auto actual_raw = static_cast<std::uint8_t>((frame.data[4] >> 1U) & 0x0fU);
  const auto selector = selector_from_raw(selector_raw);
  const auto actual_gear = actual_gear_from_raw(actual_raw);
  const bool selector_valid = selector != SelectorPosition::Unknown;
  const bool actual_gear_valid = actual_gear != ActualGear::Unknown;

  bool updated = false;
  if (selector_valid)
    updated = state.selector_position.update(selector, frame.timestamp_us) || updated;
  if (actual_gear_valid)
    updated = state.actual_gear.update(actual_gear, frame.timestamp_us) || updated;
  if (updated) {
    if (frame.timestamp_us > state.timestamp_us)
      state.timestamp_us = frame.timestamp_us;
    return DecodeStatus::Updated;
  }
  return DecodeStatus::Invalid;
}

DecodeStatus decode_doors(const RawCanFrame &frame, VehicleState &state) noexcept {
  if (!candidate_frame(frame, kDoorsId))
    return DecodeStatus::Ignored;
  if (frame.dlc != kCandidateDlc)
    return DecodeStatus::Invalid;

  // These are DBC Motorola single-bit fields. For single-bit fields the DBC
  // start bit maps directly to the byte/LSB mask used here.
  const bool liftgate_open = (frame.data[4] & 0x01U) != 0;
  const bool rear_right_door_open = (frame.data[4] & 0x04U) != 0;
  const bool rear_left_door_open = (frame.data[4] & 0x08U) != 0;
  const bool front_left_door_open_rhd = (frame.data[4] & 0x10U) != 0;
  const bool front_right_door_open_rhd = (frame.data[4] & 0x20U) != 0;
  const bool doors_unlocked = (frame.data[3] & 0x40U) != 0;

  bool updated = false;
  updated = state.liftgate_open.update(liftgate_open, frame.timestamp_us) || updated;
  updated = state.rear_right_door_open.update(rear_right_door_open, frame.timestamp_us) || updated;
  updated = state.rear_left_door_open.update(rear_left_door_open, frame.timestamp_us) || updated;
  updated = state.front_left_door_open_rhd.update(front_left_door_open_rhd, frame.timestamp_us) ||
            updated;
  updated = state.front_right_door_open_rhd.update(front_right_door_open_rhd, frame.timestamp_us) ||
            updated;
  updated = state.doors_unlocked.update(doors_unlocked, frame.timestamp_us) || updated;
  if (updated) {
    if (frame.timestamp_us > state.timestamp_us)
      state.timestamp_us = frame.timestamp_us;
    return DecodeStatus::Updated;
  }
  return DecodeStatus::Invalid;
}

DecodeStatus decode_blink_info(const RawCanFrame &frame, VehicleState &state) noexcept {
  if (!candidate_frame(frame, kBlinkInfoId))
    return DecodeStatus::Ignored;
  if (frame.dlc != kCandidateDlc)
    return DecodeStatus::Invalid;

  // LEFT_BLINK is Intel in the DBC (bit 18), while the other two confirmed
  // fields use Motorola notation. Their single-bit payload masks are 0x04,
  // 0x08, and 0x02 respectively.
  const bool left_indicator_lamp = (frame.data[2] & 0x04U) != 0;
  const bool right_indicator_lamp = (frame.data[2] & 0x08U) != 0;
  const bool wiper_low = (frame.data[4] & 0x02U) != 0;

  bool updated = false;
  updated = state.left_indicator_lamp.update(left_indicator_lamp, frame.timestamp_us) || updated;
  updated = state.right_indicator_lamp.update(right_indicator_lamp, frame.timestamp_us) || updated;
  updated = state.wiper_low.update(wiper_low, frame.timestamp_us) || updated;
  if (updated) {
    if (frame.timestamp_us > state.timestamp_us)
      state.timestamp_us = frame.timestamp_us;
    return DecodeStatus::Updated;
  }
  return DecodeStatus::Invalid;
}

DecodeStatus decode_turn_switch(const RawCanFrame &frame, VehicleState &state,
                                std::optional<TurnEdgeEvent> *edge) noexcept {
  if (edge != nullptr)
    edge->reset();
  if (!candidate_frame(frame, kTurnSwitchId))
    return DecodeStatus::Ignored;
  if (frame.dlc != kCandidateDlc)
    return DecodeStatus::Invalid;

  // DBC fields 10, 12, and 13 map to byte 1 bits 2, 4, and 5 when using
  // byte/LSB numbering. Hazard has precedence over either direction.
  const bool hazard = (frame.data[1] & (1U << 2U)) != 0;
  const bool right = (frame.data[1] & (1U << 4U)) != 0;
  const bool left = (frame.data[1] & (1U << 5U)) != 0;
  const auto front_wiper_raw = static_cast<std::uint8_t>((frame.data[2] >> 4U) & 0x03U);
  const auto front_wiper = front_wiper_from_raw(front_wiper_raw);
  const auto turn = normalize_turn(hazard, left, right);

  const auto turn_edge = state.update_turn(turn, frame.timestamp_us);
  if (edge != nullptr)
    *edge = turn_edge;
  bool updated = turn_edge.has_value();
  updated = state.hazard_request.update(hazard, frame.timestamp_us) || updated;
  updated = state.left_turn_request.update(left, frame.timestamp_us) || updated;
  updated = state.right_turn_request.update(right, frame.timestamp_us) || updated;
  updated = state.front_wiper.update(front_wiper, frame.timestamp_us) || updated;
  if (updated && frame.timestamp_us > state.timestamp_us)
    state.timestamp_us = frame.timestamp_us;
  return updated ? DecodeStatus::Updated : DecodeStatus::Invalid;
}

DecodeStatus decode(const RawCanFrame &frame, VehicleState &state,
                    std::optional<TurnEdgeEvent> *edge) noexcept {
  if (edge != nullptr)
    edge->reset();
  const auto engine_status = decode_engine_data(frame, state);
  if (engine_status != DecodeStatus::Ignored)
    return engine_status;
  const auto gear_status = decode_gear(frame, state);
  if (gear_status != DecodeStatus::Ignored)
    return gear_status;
  const auto doors_status = decode_doors(frame, state);
  if (doors_status != DecodeStatus::Ignored)
    return doors_status;
  const auto blink_status = decode_blink_info(frame, state);
  if (blink_status != DecodeStatus::Ignored)
    return blink_status;
  return decode_turn_switch(frame, state, edge);
}

} // namespace vehicle_core::mazda_candidate
