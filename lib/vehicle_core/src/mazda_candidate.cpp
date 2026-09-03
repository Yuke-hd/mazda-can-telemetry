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
    return ActualGear::Park;
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

} // namespace

DecodeStatus decode_engine_data(const RawCanFrame &frame, VehicleState &state) noexcept {
  if (!candidate_frame(frame, kEngineDataId))
    return DecodeStatus::Ignored;
  if (frame.dlc != kCandidateDlc)
    return DecodeStatus::Invalid;

  const auto rpm_raw = big_endian_u16(frame.data, 0);
  const auto speed_raw = big_endian_u16(frame.data, 2);
  // The candidate DBC declares RPM in [0, 8500]. SPEED has no invalid
  // sentinel and its 16-bit representation is already non-negative.
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

DecodeStatus decode(const RawCanFrame &frame, VehicleState &state) noexcept {
  const auto engine_status = decode_engine_data(frame, state);
  if (engine_status != DecodeStatus::Ignored)
    return engine_status;
  return decode_gear(frame, state);
}

} // namespace vehicle_core::mazda_candidate
