#pragma once

#include <cstdint>
#include <optional>

#include "vehicle_core/time.hpp"

namespace vehicle_core {

enum class SignalStatus : std::uint8_t { Unknown, Valid, Stale };

// Generic metadata units. Mazda model/enumeration types live in lib/mazda;
// the signal primitive does not depend on any vehicle definition.
enum class SignalUnit : std::uint8_t {
  None,
  KilometresPerHour,
  RevolutionsPerMinute,
  Boolean,
};

template <typename T> struct Signal {
  T value{};
  SignalUnit unit{SignalUnit::None};
  MonotonicTimestamp last_update_us{0};
  std::optional<Microseconds> freshness_timeout_us{};
  SignalStatus status{SignalStatus::Unknown};

  constexpr Signal() noexcept = default;
  constexpr explicit Signal(SignalUnit signal_unit,
                            std::optional<Microseconds> timeout_us = std::nullopt) noexcept
      : unit(signal_unit), freshness_timeout_us(timeout_us) {}

  [[nodiscard]] constexpr bool is_valid() const noexcept { return status == SignalStatus::Valid; }
  [[nodiscard]] constexpr bool is_stale() const noexcept { return status == SignalStatus::Stale; }
  [[nodiscard]] constexpr bool is_unknown() const noexcept {
    return status == SignalStatus::Unknown;
  }

  // Updates are monotonic. A late frame is rejected and cannot revive an old
  // value or move the signal's clock backwards.
  bool update(T new_value, MonotonicTimestamp timestamp_us) noexcept;

  void set_freshness_timeout(std::optional<Microseconds> timeout_us) noexcept {
    freshness_timeout_us = timeout_us;
  }

  // A zero value is still valid after update(); status is never inferred from
  // value. Unknown remains unknown until its first update.
  void refresh(MonotonicTimestamp now_us) noexcept;
};

template <typename T> bool Signal<T>::update(T new_value, MonotonicTimestamp timestamp) noexcept {
  if (status != SignalStatus::Unknown && timestamp < last_update_us) {
    return false;
  }
  value = new_value;
  last_update_us = timestamp;
  status = SignalStatus::Valid;
  return true;
}

template <typename T> void Signal<T>::refresh(MonotonicTimestamp now) noexcept {
  if (status != SignalStatus::Valid || now < last_update_us) {
    return;
  }
  if (!freshness_timeout_us.has_value()) {
    if (now > last_update_us) {
      status = SignalStatus::Stale;
    }
    return;
  }
  if ((now - last_update_us) > *freshness_timeout_us) {
    status = SignalStatus::Stale;
  }
}

} // namespace vehicle_core
