#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <initializer_list>

#include "mazda/availability.hpp"
#include "mazda/decoder.hpp"

namespace {

vehicle_core::RawCanFrame frame(const std::uint32_t identifier,
                                const vehicle_core::MonotonicTimestamp timestamp,
                                std::initializer_list<std::uint8_t> bytes,
                                const std::uint8_t dlc = 8) {
  vehicle_core::RawCanFrame result{};
  result.identifier = identifier;
  result.timestamp_us = timestamp;
  result.dlc = dlc;
  std::copy(bytes.begin(), bytes.end(), result.data.begin());
  return result;
}

} // namespace

TEST_CASE("availability distinguishes t0 data from no data and clamps backwards time") {
  vehicle_core::Signal<float> speed{vehicle_core::SignalUnit::KilometresPerHour, 250'000};
  CHECK(speed.status_at(0) == mazda::Availability::NoData);
  CHECK(speed.snapshot(0).availability == mazda::Availability::NoData);

  REQUIRE(speed.update(0.0F, 0));
  CHECK(speed.has_value);
  CHECK(speed.snapshot(0).availability == mazda::Availability::Fresh);
  CHECK(speed.snapshot(250'000).availability == mazda::Availability::Fresh);
  CHECK(speed.snapshot(250'001).availability == mazda::Availability::Stale);
  CHECK(speed.snapshot(0).availability == mazda::Availability::Fresh);
  CHECK(speed.snapshot(0).value == doctest::Approx(0.0F));
}

TEST_CASE("unconfigured freshness is explicit and snapshots do not mutate state") {
  vehicle_core::Signal<float> speed{vehicle_core::SignalUnit::KilometresPerHour};
  REQUIRE(speed.update(12.5F, 100));

  const auto before = speed;
  const auto reading = mazda::snapshot(speed, 101);
  CHECK(reading.availability == mazda::Availability::FreshnessUnverified);
  REQUIRE(reading.value.has_value());
  CHECK(*reading.value == doctest::Approx(12.5F));
  CHECK(speed.has_value == before.has_value);
  CHECK(speed.status == before.status);
  CHECK(speed.last_update_us == before.last_update_us);
}

TEST_CASE("undefined signal preserves its value but is unavailable until newer recovery") {
  vehicle_core::Signal<int> signal{};
  REQUIRE(signal.update(7, 100));
  CHECK_FALSE(signal.invalidate(99));
  CHECK(signal.is_valid());
  CHECK(signal.snapshot(100).availability == mazda::Availability::FreshnessUnverified);

  CHECK(signal.invalidate(101));
  const auto unavailable = signal.snapshot(101);
  CHECK(unavailable.availability == mazda::Availability::Unavailable);
  REQUIRE(unavailable.value.has_value());
  CHECK(*unavailable.value == 7);
  CHECK_FALSE(signal.invalidate(99));

  REQUIRE(signal.update(8, 102));
  CHECK(signal.snapshot(102).availability == mazda::Availability::FreshnessUnverified);
  CHECK(signal.value == 8);
}

TEST_CASE("message watermark rejects old and conflicting data and requires newer recovery") {
  mazda::VehicleState state{};
  vehicle_core::HealthObservation health{};
  health.transport = vehicle_core::TransportHealth::Live;

  const auto first = frame(mazda::candidate::kEngineDataId, 100, {0x00, 0x01, 0, 0, 0, 0, 0, 0});
  REQUIRE(mazda::candidate::decode_engine_data(first, state, nullptr, &health) ==
          mazda::candidate::DecodeStatus::Decoded);
  CHECK(health.message == vehicle_core::MessageHealth::Healthy);
  CHECK(health.has_last_frame);
  CHECK(health.last_frame_us == 100);
  CHECK(health.has_last_accepted);
  CHECK(health.last_accepted_us == 100);
  CHECK_FALSE(health.fault_timestamp_us.has_value());
  CHECK(state.engine_rpm.value == doctest::Approx(0.25F));

  const auto duplicate = first;
  REQUIRE(mazda::candidate::decode_engine_data(duplicate, state) ==
          mazda::candidate::DecodeStatus::Decoded);
  CHECK(state.engine_rpm.value == doctest::Approx(0.25F));
  CHECK(state.engine_rpm.last_update_us == 100);

  const auto conflicting =
      frame(mazda::candidate::kEngineDataId, 100, {0x00, 0x02, 0, 0, 0, 0, 0, 0});
  REQUIRE(mazda::candidate::decode_engine_data(conflicting, state) ==
          mazda::candidate::DecodeStatus::Decoded);
  CHECK(state.engine_rpm.value == doctest::Approx(0.25F));
  CHECK(state.health_observation(mazda::candidate::kEngineDataId).message ==
        vehicle_core::MessageHealth::Healthy);

  const auto old = frame(mazda::candidate::kEngineDataId, 99, {0x00, 0x03, 0, 0, 0, 0, 0, 0});
  REQUIRE(mazda::candidate::decode_engine_data(old, state) ==
          mazda::candidate::DecodeStatus::Decoded);
  CHECK(state.engine_rpm.value == doctest::Approx(0.25F));
  CHECK(state.engine_rpm.last_update_us == 100);

  const auto malformed =
      frame(mazda::candidate::kEngineDataId, 100, {0x84, 0xd1, 0, 0, 0, 0, 0, 0});
  REQUIRE(mazda::candidate::decode_engine_data(malformed, state, nullptr, &health) ==
          mazda::candidate::DecodeStatus::Malformed);
  CHECK(health.message == vehicle_core::MessageHealth::Faulted);
  CHECK(health.fault_timestamp_us == 100);
  CHECK(state.engine_rpm.value == doctest::Approx(0.25F));

  // Equal-time valid data cannot clear a same-time malformed fault.
  REQUIRE(mazda::candidate::decode_engine_data(first, state) ==
          mazda::candidate::DecodeStatus::Decoded);
  CHECK(state.health_observation(mazda::candidate::kEngineDataId).message ==
        vehicle_core::MessageHealth::Faulted);
  CHECK(state.engine_rpm.value == doctest::Approx(0.25F));

  const auto recovered =
      frame(mazda::candidate::kEngineDataId, 101, {0x00, 0x04, 0, 0, 0, 0, 0, 0});
  REQUIRE(mazda::candidate::decode_engine_data(recovered, state, nullptr, &health) ==
          mazda::candidate::DecodeStatus::Decoded);
  CHECK(health.message == vehicle_core::MessageHealth::Healthy);
  CHECK_FALSE(health.fault_timestamp_us.has_value());
  CHECK(health.last_accepted_us == 101);
  CHECK(state.engine_rpm.value == doctest::Approx(1.0F));
}

TEST_CASE("undefined gear isolates one signal and relevant faults do not blank unrelated turn") {
  mazda::VehicleState state{};
  const auto supported_gear = frame(mazda::candidate::kGearId, 10, {0x04, 0, 0, 0, 0x1c, 0, 0, 0});
  REQUIRE(mazda::candidate::decode_gear(supported_gear, state) ==
          mazda::candidate::DecodeStatus::Decoded);
  CHECK(state.selector_position.is_valid());
  CHECK(state.actual_gear.is_valid());

  // Selector 5 is undefined while actual gear remains Reverse. Only the
  // selector becomes non-actionable; the message itself remains healthy.
  const auto undefined_selector =
      frame(mazda::candidate::kGearId, 11, {0x05, 0, 0, 0, 0x1c, 0, 0, 0});
  REQUIRE(mazda::candidate::decode_gear(undefined_selector, state) ==
          mazda::candidate::DecodeStatus::Decoded);
  CHECK(mazda::snapshot(state.selector_position, 11).availability ==
        mazda::Availability::Unavailable);
  CHECK(state.selector_position.value == mazda::SelectorPosition::Drive);
  CHECK(mazda::snapshot(state.actual_gear, 11).availability ==
        mazda::Availability::FreshnessUnverified);
  CHECK(state.actual_gear.value == mazda::ActualGear::Reverse);
  CHECK(state.health_observation(mazda::candidate::kGearId).message ==
        vehicle_core::MessageHealth::Healthy);

  const auto turn = frame(mazda::candidate::kTurnSwitchId, 20, {0, 0x20, 0, 0, 0, 0, 0, 0});
  REQUIRE(mazda::candidate::decode_turn_switch(turn, state) ==
          mazda::candidate::DecodeStatus::Decoded);
  CHECK(mazda::snapshot(state.turn_state, 20).availability == mazda::Availability::Fresh);

  // A malformed gear message affects only the gear message's readings. The
  // independently decoded turn signal stays actionable.
  const auto short_gear = frame(mazda::candidate::kGearId, 21, {0x04, 0, 0, 0, 0x1c, 0, 0, 0}, 7);
  REQUIRE(mazda::candidate::decode_gear(short_gear, state) ==
          mazda::candidate::DecodeStatus::Malformed);
  CHECK(state.reading_at(state.selector_position, mazda::candidate::kGearId, 21).availability ==
        mazda::Availability::Unavailable);
  CHECK(state.reading_at(state.actual_gear, mazda::candidate::kGearId, 21).availability ==
        mazda::Availability::Unavailable);
  CHECK(state.reading_at(state.turn_state, mazda::candidate::kTurnSwitchId, 21).availability ==
        mazda::Availability::Fresh);
  CHECK(state.selector_position.value == mazda::SelectorPosition::Drive);
  CHECK(state.actual_gear.value == mazda::ActualGear::Reverse);

  const auto repaired_gear = frame(mazda::candidate::kGearId, 22, {0x04, 0, 0, 0, 0x1c, 0, 0, 0});
  REQUIRE(mazda::candidate::decode_gear(repaired_gear, state) ==
          mazda::candidate::DecodeStatus::Decoded);
  CHECK(state.reading_at(state.selector_position, mazda::candidate::kGearId, 22).availability ==
        mazda::Availability::FreshnessUnverified);
  CHECK(state.reading_at(state.actual_gear, mazda::candidate::kGearId, 22).availability ==
        mazda::Availability::FreshnessUnverified);
}

TEST_CASE("transport and message health remain separate availability inputs") {
  vehicle_core::Signal<bool> lamp{vehicle_core::SignalUnit::Boolean, 250'000};
  REQUIRE(lamp.update(false, 0));

  CHECK(mazda::status_at(lamp, 250'000, vehicle_core::MessageHealth::Healthy,
                         vehicle_core::TransportHealth::Live) == mazda::Availability::Fresh);
  CHECK(mazda::status_at(lamp, 250'000, vehicle_core::MessageHealth::Faulted,
                         vehicle_core::TransportHealth::Live) == mazda::Availability::Unavailable);
  CHECK(mazda::status_at(lamp, 0, vehicle_core::MessageHealth::Healthy,
                         vehicle_core::TransportHealth::TimedOut) ==
        mazda::Availability::Unavailable);
  CHECK(mazda::status_at(lamp, 0, vehicle_core::MessageHealth::Healthy,
                         vehicle_core::TransportHealth::AwaitingTraffic) ==
        mazda::Availability::Fresh);
}
