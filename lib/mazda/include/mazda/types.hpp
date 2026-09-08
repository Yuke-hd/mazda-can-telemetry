#pragma once

#include <cstdint>

#include "vehicle_core/time.hpp"

namespace mazda {

enum class SelectorPosition : std::uint8_t { Unknown, Park, Reverse, Neutral, Drive };

enum class ActualGear : std::uint8_t {
  Unknown,
  Park,
  Neutral,
  Reverse,
  First,
  Second,
  Third,
  Fourth,
  Fifth,
  Sixth,
  // The DBC's raw zero value is labelled P_or_N. Keep the existing Park
  // representation source-compatible while exposing the source label.
  ParkOrNeutral = Park,
};

enum class FrontWiperPosition : std::uint8_t { Unknown, Off, On, High, Intermittent };

enum class TurnState : std::uint8_t { Unknown, Off, Left, Right, Hazard };

enum class TurnEventType : std::uint8_t { StateChanged };

// Semantic event: intentionally contains no Mazda CAN identifier or raw
// payload. It is safe to pass to a dashboard or effect consumer.
struct TurnEdgeEvent {
  TurnEventType type{TurnEventType::StateChanged};
  TurnState previous{TurnState::Unknown};
  TurnState current{TurnState::Unknown};
  vehicle_core::MonotonicTimestamp timestamp_us{0};
};

} // namespace mazda
