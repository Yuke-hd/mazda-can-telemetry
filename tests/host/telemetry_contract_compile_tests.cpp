#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <type_traits>

#include "local_argb/lighting_sink.hpp"
#include "mazda/vehicle_telemetry.hpp"
#include "vehicle_core/decoder_contracts.hpp"
#include "vehicle_core/notification.hpp"
#include "vehicle_core/reading.hpp"

namespace {
void turn_callback(void *, const mazda::Notification<mazda::TurnState> &) noexcept {}
void bool_callback(void *, const mazda::Notification<bool> &) noexcept {}
} // namespace

TEST_CASE("telemetry contracts are value-copy and callback ABI seams") {
  static_assert(std::is_trivially_copyable_v<vehicle_core::Reading<float>>);
  static_assert(std::is_trivially_copyable_v<vehicle_core::Notification<bool>>);
  static_assert(mazda::kNotifySubscribersPerChannel == 2);

  mazda::Reading<float> speed{};
  speed.value = 12.5F;
  speed.availability = mazda::Availability::FreshnessUnverified;
  speed.validation = mazda::ValidationStatus::Observed;
  CHECK(*speed.value == doctest::Approx(12.5F));

  mazda::Notification<mazda::TurnState> notice{};
  notice.current = mazda::Reading<mazda::TurnState>{
      mazda::TurnState::Left, mazda::Availability::Fresh, mazda::ValidationStatus::Confirmed};
  notice.initial = true;
  notice.became_unavailable = true;
  notice.recovered = true;
  notice.coalesced = true;
  CHECK(notice.current.value == mazda::TurnState::Left);
  CHECK(notice.initial);

  const mazda::Callback<mazda::TurnState> turn = &turn_callback;
  const mazda::Callback<bool> boolean = &bool_callback;
  CHECK(turn != nullptr);
  CHECK(boolean != nullptr);
}

TEST_CASE("facade exposes fixed polling and notify channels without implementation") {
  using Facade = mazda::VehicleTelemetry;
  static_assert(!std::is_copy_constructible_v<Facade>);
  static_assert(!std::is_move_constructible_v<Facade>);

  using SpeedFn = decltype(&Facade::speed_kph);
  using RpmFn = decltype(&Facade::engine_rpm);
  using TurnFn = decltype(&Facade::on_turn_state_changed);
  using DoorsFn = decltype(&Facade::on_doors_unlocked_changed);
  using UnsubscribeFn = decltype(&Facade::unsubscribe);
  using DiagnosticsFn = decltype(&Facade::diagnostics);
  static_assert(std::is_member_function_pointer_v<SpeedFn>);
  static_assert(std::is_member_function_pointer_v<RpmFn>);
  static_assert(std::is_member_function_pointer_v<TurnFn>);
  static_assert(std::is_member_function_pointer_v<DoorsFn>);
  static_assert(std::is_member_function_pointer_v<UnsubscribeFn>);
  static_assert(std::is_member_function_pointer_v<DiagnosticsFn>);

  CHECK(mazda::TelemetryConfig{}.transport_silence_timeout_us == 1'000'000);
  CHECK(mazda::TelemetryConfig{}.callback_stop_timeout_us == 500'000);
}

TEST_CASE("decoder and private lighting contracts keep health separate") {
  vehicle_core::DecoderObservation observation{};
  observation.validity = vehicle_core::DecodeValidity::Malformed;
  observation.timestamp_us = 100;
  CHECK(observation.validity == vehicle_core::DecodeValidity::Malformed);

  local_argb::internal::LightingCommand command{};
  command.actionable = false;
  command.valid_until_us = 200;
  CHECK_FALSE(command.actionable);
  CHECK(command.valid_until_us == 200);
}
