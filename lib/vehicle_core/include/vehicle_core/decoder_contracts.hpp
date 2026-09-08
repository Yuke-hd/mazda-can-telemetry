#pragma once

#include <cstdint>
#include <optional>

#include "vehicle_core/frame.hpp"
#include "vehicle_core/health.hpp"

namespace vehicle_core {

enum class DecodeValidity : std::uint8_t { Ignored, Decoded, Malformed };

// A decoder observation carries ownership/validity separately from semantic
// values. A malformed message may affect one signal without erasing another.
struct DecoderObservation {
  DecodeValidity validity{DecodeValidity::Ignored};
  MonotonicTimestamp timestamp_us{0};
  std::uint32_t identifier{0};
  std::uint8_t bus_id{0};
  std::uint8_t dlc{0};
};

struct HealthObservation {
  TransportHealth transport{TransportHealth::Stopped};
  MessageHealth message{MessageHealth::Unknown};
  SignalHealth signal{SignalHealth::NoData};
  bool has_last_frame{false};
  MonotonicTimestamp last_frame_us{0};
  bool has_last_accepted{false};
  MonotonicTimestamp last_accepted_us{0};
  std::optional<MonotonicTimestamp> fault_timestamp_us{};
};

} // namespace vehicle_core
