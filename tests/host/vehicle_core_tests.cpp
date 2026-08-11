#include <doctest/doctest.h>

#include "vehicle_core/vehicle_core.hpp"

TEST_CASE("unknown state has no usable signals") {
  const auto state = vehicle_core::make_unknown_state(1234);

  CHECK(state.timestamp_us == 1234);
  CHECK(state.speed_kph.validity == vehicle_core::Validity::Unknown);
  CHECK(state.engine_rpm.validity == vehicle_core::Validity::Unknown);
  CHECK(state.gear.validity == vehicle_core::Validity::Unknown);
  CHECK(state.turn.value == vehicle_core::TurnState::Unknown);
}

TEST_CASE("freshness is bounded by the signal timeout") {
  vehicle_core::Signal<float> signal{};
  signal.value = 42.0F;
  signal.last_update_us = 1'000;
  signal.validity = vehicle_core::Validity::Valid;

  CHECK(vehicle_core::is_fresh(signal, 1'250));
  CHECK_FALSE(vehicle_core::is_fresh(signal, 1'251));
  CHECK_FALSE(vehicle_core::is_fresh(signal, 999));
}
