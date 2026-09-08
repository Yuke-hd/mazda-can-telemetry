#pragma once

#include <cstdint>
#include <optional>

#include "vehicle_core/telemetry_contracts.hpp"
#include "vehicle_core/time.hpp"

namespace vehicle_core {

enum class SignalStatus : std::uint8_t { Unknown, Valid, Stale };

// update_status() distinguishes a newly accepted value from an idempotent
// duplicate and from a rejected timestamp/value. The bool update() wrapper
// remains source-compatible with the Stage 0/1-A surface.
enum class SignalUpdateResult : std::uint8_t { Rejected, Idempotent, Updated };

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
  // last_update_us is meaningful only when has_value is true. This explicit
  // bit keeps a valid observation at t=0 distinct from NoData.
  bool has_value{false};
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

  [[nodiscard]] constexpr bool is_unavailable() const noexcept {
    return has_value && status == SignalStatus::Unknown;
  }

  // Updates are monotonic. A late frame is rejected and cannot revive an old
  // value or move the signal's clock backwards.
  SignalUpdateResult update_status(T new_value, MonotonicTimestamp timestamp_us) noexcept;

  bool update(T new_value, MonotonicTimestamp timestamp_us) noexcept {
    return update_status(new_value, timestamp_us) != SignalUpdateResult::Rejected;
  }

  // Invalidate the semantic value without erasing the last accepted value or
  // moving its timestamp. A later strictly newer update is required to make
  // it actionable again.
  bool invalidate(MonotonicTimestamp timestamp_us) noexcept;

  void set_freshness_timeout(std::optional<Microseconds> timeout_us) noexcept {
    freshness_timeout_us = timeout_us;
  }

  // A zero value is still valid after update(); status is never inferred from
  // value. Unknown remains unknown until its first update.
  void refresh(MonotonicTimestamp now_us) noexcept;

  [[nodiscard]] Availability status_at(MonotonicTimestamp now_us) const noexcept;

  [[nodiscard]] Reading<T>
  snapshot(MonotonicTimestamp now_us,
           ValidationStatus validation = ValidationStatus::Reference) const noexcept;
};

template <typename T>
SignalUpdateResult Signal<T>::update_status(T new_value, MonotonicTimestamp timestamp) noexcept {
  if (!has_value) {
    value = new_value;
    last_update_us = timestamp;
    has_value = true;
    status = SignalStatus::Valid;
    return SignalUpdateResult::Updated;
  }
  if (timestamp < last_update_us) {
    return SignalUpdateResult::Rejected;
  }
  if (timestamp == last_update_us) {
    // Equal-time healthy duplicates are harmless. A different value at the
    // same timestamp is a conflicting observation and must not overwrite the
    // accepted value or recover an unavailable signal.
    return value == new_value ? SignalUpdateResult::Idempotent : SignalUpdateResult::Rejected;
  }
  value = new_value;
  last_update_us = timestamp;
  status = SignalStatus::Valid;
  return SignalUpdateResult::Updated;
}

template <typename T> bool Signal<T>::invalidate(const MonotonicTimestamp timestamp) noexcept {
  if (has_value && timestamp < last_update_us) {
    return false;
  }
  status = SignalStatus::Unknown;
  return true;
}

template <typename T> void Signal<T>::refresh(MonotonicTimestamp now) noexcept {
  if (status != SignalStatus::Valid || now < last_update_us) {
    return;
  }
  if (!freshness_timeout_us.has_value()) {
    // Keep the legacy SignalStatus mutation for callers that use refresh() to
    // obtain a conservative raw-state view. The pure status_at()/snapshot()
    // methods evaluate an unconfigured signal as FreshnessUnverified without
    // changing the source object.
    if (now > last_update_us) {
      status = SignalStatus::Stale;
    }
    return;
  }
  if ((now - last_update_us) > *freshness_timeout_us) {
    status = SignalStatus::Stale;
  }
}

template <typename T>
Availability Signal<T>::status_at(const MonotonicTimestamp now) const noexcept {
  if (!has_value) {
    return Availability::NoData;
  }
  if (status == SignalStatus::Unknown) {
    return Availability::Unavailable;
  }
  if (!freshness_timeout_us.has_value()) {
    return Availability::FreshnessUnverified;
  }
  const auto age = now >= last_update_us ? now - last_update_us : 0;
  return age <= *freshness_timeout_us ? Availability::Fresh : Availability::Stale;
}

template <typename T>
Reading<T> Signal<T>::snapshot(const MonotonicTimestamp now,
                               const ValidationStatus validation) const noexcept {
  Reading<T> result{};
  result.availability = status_at(now);
  result.validation = validation;
  if (has_value) {
    result.value = value;
  }
  return result;
}

} // namespace vehicle_core
