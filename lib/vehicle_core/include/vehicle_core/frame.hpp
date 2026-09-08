#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "vehicle_core/time.hpp"

namespace vehicle_core {

constexpr std::size_t kCanClassicPayloadBytes = 8;

enum class CanIdentifierFormat : std::uint8_t { Standard, Extended };

// A receive-only, classic-CAN frame. The fixed payload keeps the type safe to
// copy through bounded queues and prevents ownership of a buffer from leaking
// through the portable API.
struct RawCanFrame {
  MonotonicTimestamp timestamp_us{0};
  std::uint8_t bus_id{0};
  std::uint32_t identifier{0};
  CanIdentifierFormat identifier_format{CanIdentifierFormat::Standard};
  bool remote_request{false};
  std::uint8_t dlc{0};
  std::array<std::uint8_t, kCanClassicPayloadBytes> data{};

  [[nodiscard]] bool is_valid() const noexcept;
  [[nodiscard]] bool is_extended() const noexcept {
    return identifier_format == CanIdentifierFormat::Extended;
  }
};

} // namespace vehicle_core
