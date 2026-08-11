#pragma once

#include <array>
#include <cstdint>

namespace vehicle_core {

constexpr std::uint8_t kMaxCanDataLength = 8;

struct RawCanFrame {
  std::uint64_t timestamp_us{0};
  std::uint8_t bus{0};
  std::uint32_t id{0};
  bool is_extended{false};
  bool is_rtr{false};
  std::uint8_t dlc{0};
  std::array<std::uint8_t, kMaxCanDataLength> data{};
};

enum class Validity : std::uint8_t {
  Unknown,
  Valid,
  Stale,
};

template <typename T> struct Signal {
  T value{};
  std::uint64_t last_update_us{0};
  Validity validity{Validity::Unknown};
};

enum class TurnState : std::uint8_t {
  Unknown,
  Off,
  Left,
  Right,
  Hazard,
};

struct VehicleState {
  std::uint64_t timestamp_us{0};
  Signal<float> speed_kph{};
  Signal<float> engine_rpm{};
  Signal<std::int8_t> gear{};
  Signal<TurnState> turn{};
};

constexpr std::uint64_t kDefaultSignalTimeoutUs = 250'000;

bool is_fresh(const Signal<float> &signal, std::uint64_t now_us,
              std::uint64_t timeout_us = kDefaultSignalTimeoutUs);

VehicleState make_unknown_state(std::uint64_t timestamp_us);

} // namespace vehicle_core
