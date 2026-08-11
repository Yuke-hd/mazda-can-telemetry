#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <type_traits>

#include "vehicle_core/vehicle_core.hpp"

namespace {

class FakeClock final : public vehicle_core::MonotonicClock {
public:
  vehicle_core::MonotonicTimestamp time_us{0};

  [[nodiscard]] vehicle_core::MonotonicTimestamp now() const noexcept override { return time_us; }
};

} // namespace

TEST_CASE("raw frame construction retains bus and fixed payload") {
  vehicle_core::RawCanFrame frame{};
  frame.timestamp_us = 42;
  frame.bus_id = 3;
  frame.identifier = 0x123;
  frame.dlc = 2;
  frame.data[0] = 0xaa;
  frame.data[1] = 0x55;

  CHECK(frame.is_valid());
  CHECK(frame.bus_id == 3);
  CHECK(frame.timestamp_us == 42);
  CHECK(frame.data[0] == 0xaa);
  CHECK(frame.data[1] == 0x55);
  CHECK(frame.is_extended() == false);
}

TEST_CASE("raw frame validates classic CAN identifier and DLC bounds") {
  vehicle_core::RawCanFrame frame{};
  frame.identifier = 0x800;
  CHECK(frame.is_valid() == false);
  frame.identifier = 0x1fffffff;
  frame.identifier_format = vehicle_core::CanIdentifierFormat::Extended;
  frame.dlc = 8;
  CHECK(frame.is_valid());
  frame.dlc = 9;
  CHECK(frame.is_valid() == false);
}

TEST_CASE("signal status distinguishes valid zero from unknown and stale") {
  vehicle_core::Signal<float> speed{vehicle_core::SignalUnit::KilometresPerHour, 100};
  CHECK(speed.is_unknown());
  CHECK(speed.update(0.0F, 100));
  CHECK(speed.is_valid());
  CHECK(speed.value == 0.0F);

  speed.refresh(200);
  CHECK(speed.is_valid());
  speed.refresh(201);
  CHECK(speed.is_stale());
  CHECK(speed.value == 0.0F);

  vehicle_core::Signal<float> never_updated{vehicle_core::SignalUnit::KilometresPerHour, 1};
  never_updated.refresh(10000);
  CHECK(never_updated.is_unknown());
}

TEST_CASE("signal rejects out of order timestamps") {
  vehicle_core::Signal<int> rpm{vehicle_core::SignalUnit::RevolutionsPerMinute};
  CHECK(rpm.update(1000, 500));
  CHECK(rpm.update(1100, 501));
  CHECK(rpm.update(900, 500) == false);
  CHECK(rpm.value == 1100);
  CHECK(rpm.last_update_us == 501);
  CHECK(rpm.is_valid());
}

TEST_CASE("turn updates create semantic edge events without CAN identity") {
  vehicle_core::VehicleState state{};
  CHECK(state.turn_state.is_unknown());

  const auto first = state.update_turn(vehicle_core::TurnState::Left, 10);
  REQUIRE(first.has_value());
  CHECK(first->previous == vehicle_core::TurnState::Unknown);
  CHECK(first->current == vehicle_core::TurnState::Left);
  CHECK(first->timestamp_us == 10);

  CHECK_FALSE(state.update_turn(vehicle_core::TurnState::Left, 11).has_value());
  const auto second = state.update_turn(vehicle_core::TurnState::Off, 20);
  REQUIRE(second.has_value());
  CHECK(second->previous == vehicle_core::TurnState::Left);
  CHECK(second->current == vehicle_core::TurnState::Off);
  CHECK(state.update_turn(vehicle_core::TurnState::Right, 19).has_value() == false);
}

TEST_CASE("selector position and actual gear are independent signals") {
  vehicle_core::VehicleState state{};
  REQUIRE(state.selector_position.update(vehicle_core::SelectorPosition::Drive, 100));
  REQUIRE(state.actual_gear.update(vehicle_core::ActualGear::Third, 100));

  CHECK(state.selector_position.value == vehicle_core::SelectorPosition::Drive);
  CHECK(state.actual_gear.value == vehicle_core::ActualGear::Third);
  CHECK(state.selector_position.unit == vehicle_core::SignalUnit::SelectorPosition);
  CHECK(state.actual_gear.unit == vehicle_core::SignalUnit::ActualGear);
  CHECK(static_cast<const void *>(&state.selector_position) !=
        static_cast<const void *>(&state.actual_gear));
}

TEST_CASE("snapshot applies each signal's freshness policy independently") {
  vehicle_core::VehicleState state{};
  REQUIRE(state.speed_kph.update(20.0F, 100));
  REQUIRE(state.turn_state.update(vehicle_core::TurnState::Left, 100));

  const auto snapshot = state.snapshot(350'001);
  CHECK(snapshot.speed_kph.is_valid());
  CHECK(snapshot.turn_state.is_stale());
  CHECK(state.speed_kph.is_valid());
  CHECK(state.turn_state.is_valid());
}

TEST_CASE("snapshot freshness is deterministic and does not mutate source") {
  FakeClock clock;
  vehicle_core::VehicleStateStore store{clock};
  REQUIRE(store.mutable_state().speed_kph.update(12.5F, 100));

  clock.time_us = 100'350;
  const auto fresh = store.snapshot();
  CHECK(fresh.speed_kph.is_valid());
  CHECK(fresh.timestamp_us == 100'350);

  clock.time_us = 600'001;
  const auto stale = store.snapshot();
  CHECK(stale.speed_kph.is_stale());
  CHECK(stale.speed_kph.value == doctest::Approx(12.5F));
  CHECK(store.state().speed_kph.is_valid());
}

TEST_CASE("domain API is plain portable C++") {
  CHECK(std::is_trivially_copyable_v<vehicle_core::RawCanFrame>);
  CHECK(sizeof(vehicle_core::RawCanFrame) <= 32);
  CHECK(vehicle_core::kApiVersion == 1);
  CHECK(vehicle_core::library_is_available());
}
