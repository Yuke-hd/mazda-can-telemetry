#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace vehicle_core {

constexpr std::uint32_t kApiVersion = 1;
constexpr std::size_t kCanClassicPayloadBytes = 8;

using MonotonicTimestamp = std::uint64_t;
using Microseconds = std::uint64_t;

enum class CanIdentifierFormat : std::uint8_t { Standard, Extended };

// A receive-only, classic-CAN frame. The fixed payload keeps the type safe to
// copy through bounded queues and prevents ownership of a buffer from leaking
// through the domain API.
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

enum class SignalStatus : std::uint8_t { Unknown, Valid, Stale };

// Units are identifiers rather than strings, so signal metadata has static
// storage and cannot dangle. Additions must remain transport-independent.
enum class SignalUnit : std::uint8_t {
  None,
  KilometresPerHour,
  RevolutionsPerMinute,
  Gear,
  Boolean,
  TurnState,
};

enum class GearState : std::uint8_t { Unknown, Park, Reverse, Neutral, Drive };

template <typename T> struct Signal {
  T value{};
  SignalUnit unit{SignalUnit::None};
  MonotonicTimestamp last_update_us{0};
  SignalStatus status{SignalStatus::Unknown};

  constexpr Signal() noexcept = default;
  constexpr explicit Signal(SignalUnit signal_unit) noexcept
      : unit(signal_unit) {}

  [[nodiscard]] constexpr bool is_valid() const noexcept {
    return status == SignalStatus::Valid;
  }
  [[nodiscard]] constexpr bool is_stale() const noexcept {
    return status == SignalStatus::Stale;
  }
  [[nodiscard]] constexpr bool is_unknown() const noexcept {
    return status == SignalStatus::Unknown;
  }

  // Updates are monotonic. A late frame is rejected and cannot revive an old
  // value or move the signal's clock backwards.
  bool update(T new_value, MonotonicTimestamp timestamp_us) noexcept;

  // A zero value is still valid after update(); status is never inferred from
  // value. Calling refresh with an elapsed timeout marks only a valid signal
  // stale. Unknown remains unknown until its first update.
  void refresh(MonotonicTimestamp now_us, Microseconds freshness_timeout_us) noexcept;
};

enum class TurnState : std::uint8_t { Unknown, Off, Left, Right, Hazard };

enum class TurnEventType : std::uint8_t { StateChanged };

// Semantic event: intentionally contains no Mazda CAN identifier or raw
// payload. It is safe to pass to a dashboard or effect consumer.
struct TurnEdgeEvent {
  TurnEventType type{TurnEventType::StateChanged};
  TurnState previous{TurnState::Unknown};
  TurnState current{TurnState::Unknown};
  MonotonicTimestamp timestamp_us{0};
};

struct VehicleState {
  MonotonicTimestamp timestamp_us{0};
  Signal<float> speed_kph{SignalUnit::KilometresPerHour};
  Signal<float> engine_rpm{SignalUnit::RevolutionsPerMinute};
  Signal<GearState> gear{SignalUnit::Gear};
  Signal<TurnState> turn_state{SignalUnit::TurnState};
  Signal<bool> hazard_request{SignalUnit::Boolean};
  Signal<bool> left_turn_request{SignalUnit::Boolean};
  Signal<bool> right_turn_request{SignalUnit::Boolean};

  // Apply a semantic turn state and return an edge only when the state
  // changed. The first known state has Unknown as its previous state.
  std::optional<TurnEdgeEvent> update_turn(TurnState state,
                                           MonotonicTimestamp timestamp_us) noexcept;

  // Return a value snapshot evaluated at now_us. The original state is not
  // mutated, making this suitable for independent consumers.
  [[nodiscard]] VehicleState snapshot(MonotonicTimestamp now_us,
                                       Microseconds freshness_timeout_us) const noexcept;

  void refresh(MonotonicTimestamp now_us, Microseconds freshness_timeout_us) noexcept;
};

// A deterministic source of monotonic time. Production code can adapt an
// embedded clock; host tests can use a fixed clock without sleeping.
class MonotonicClock {
public:
  virtual ~MonotonicClock() = default;
  [[nodiscard]] virtual MonotonicTimestamp now() const noexcept = 0;
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
  explicit VehicleStateStore(MonotonicClock& clock,
                             Microseconds freshness_timeout_us) noexcept;

  [[nodiscard]] VehicleState& mutable_state() noexcept { return state_; }
  [[nodiscard]] const VehicleState& state() const noexcept { return state_; }
  [[nodiscard]] VehicleState snapshot() const noexcept override;

private:
  MonotonicClock* clock_;
  Microseconds freshness_timeout_us_;
  VehicleState state_{};
};

[[nodiscard]] bool library_is_available() noexcept;

} // namespace vehicle_core

// Template definitions stay in the public header so the portable API works
// for application-defined signal value types without a runtime registry.
namespace vehicle_core {

template <typename T>
bool Signal<T>::update(T new_value, MonotonicTimestamp timestamp) noexcept {
  if (status != SignalStatus::Unknown && timestamp < last_update_us) {
    return false;
  }
  value = new_value;
  last_update_us = timestamp;
  status = SignalStatus::Valid;
  return true;
}

template <typename T>
void Signal<T>::refresh(MonotonicTimestamp now,
                        Microseconds freshness_timeout) noexcept {
  if (status != SignalStatus::Valid || now < last_update_us) {
    return;
  }
  if ((now - last_update_us) > freshness_timeout) {
    status = SignalStatus::Stale;
  }
}

} // namespace vehicle_core
