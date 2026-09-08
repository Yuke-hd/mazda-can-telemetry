#pragma once

#include <optional>

#include "mazda/definitions.hpp"
#include "mazda/state.hpp"

namespace mazda::candidate {

enum class DecodeStatus : std::uint8_t { Ignored, Updated, Invalid };

[[nodiscard]] DecodeStatus decode_engine_data(const vehicle_core::RawCanFrame &frame,
                                              VehicleState &state) noexcept;
[[nodiscard]] DecodeStatus decode_gear(const vehicle_core::RawCanFrame &frame,
                                       VehicleState &state) noexcept;
[[nodiscard]] DecodeStatus decode_doors(const vehicle_core::RawCanFrame &frame,
                                        VehicleState &state) noexcept;
[[nodiscard]] DecodeStatus decode_blink_info(const vehicle_core::RawCanFrame &frame,
                                             VehicleState &state) noexcept;
[[nodiscard]] DecodeStatus
decode_turn_switch(const vehicle_core::RawCanFrame &frame, VehicleState &state,
                   std::optional<TurnEdgeEvent> *edge = nullptr) noexcept;
// Dispatches only validated, capture-confirmed messages plus the existing
// speed candidate carried by ENGINE_DATA.
[[nodiscard]] DecodeStatus decode(const vehicle_core::RawCanFrame &frame, VehicleState &state,
                                  std::optional<TurnEdgeEvent> *edge = nullptr) noexcept;

} // namespace mazda::candidate
