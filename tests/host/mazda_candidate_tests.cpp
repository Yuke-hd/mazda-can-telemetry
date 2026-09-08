#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>

#include "../support/direct_frame_feeder.hpp"
#include "../support/fake_clock.hpp"
#include "vehicle_core/vehicle_core.hpp"

namespace {

vehicle_core::RawCanFrame frame(const std::uint32_t id, const std::uint64_t timestamp,
                                std::initializer_list<std::uint8_t> bytes,
                                const std::uint8_t dlc = 8) {
  vehicle_core::RawCanFrame result{};
  result.timestamp_us = timestamp;
  result.identifier = id;
  result.dlc = dlc;
  std::copy(bytes.begin(), bytes.end(), result.data.begin());
  return result;
}

} // namespace

TEST_CASE("confirmed definitions retain DBC provenance and exact metadata") {
  using namespace vehicle_core::mazda_candidate;
  CHECK(kEngineDataDefinition.identifier == 0x202);
  CHECK(kEngineDataDefinition.expected_dlc == 8);
  CHECK_FALSE(kEngineDataDefinition.expected_period_us.has_value());
  CHECK_FALSE(kEngineDataDefinition.freshness_timeout_us.has_value());
  CHECK_FALSE(kEngineDataDefinition.pending_validation);
  CHECK(std::string{kEngineDataDefinition.provenance}.find("capture-derived") != std::string::npos);
  CHECK(kTransmissionDefinition.name == std::string{"TRANSMISSION"});
  CHECK(kDoorsDefinition.identifier == kDoorsId);
  CHECK_FALSE(kBlinkInfoDefinition.pending_validation);
  CHECK(kEngineRpmDefinition.name == std::string{"EngineRPM"});
  CHECK(kEngineRpmDefinition.scale == doctest::Approx(0.25F));
  CHECK(kEngineSpeedDefinition.scale == doctest::Approx(0.01F));
  CHECK(kEngineRpmDefinition.unit == vehicle_core::SignalUnit::RevolutionsPerMinute);
  CHECK(kEngineSpeedDefinition.unit == vehicle_core::SignalUnit::KilometresPerHour);
  CHECK(kEngineRpmDefinition.dbc_start_bit == 7);
  CHECK(kSelectorDefinition.dbc_start_bit == 2);
  CHECK(kSelectorDefinition.physical_min == doctest::Approx(0.0F));
  CHECK(kSelectorDefinition.physical_max == doctest::Approx(7.0F));
  CHECK(kActualGearDefinition.dbc_start_bit == 36);
  CHECK(kActualGearDefinition.physical_min == doctest::Approx(0.0F));
  CHECK(kActualGearDefinition.physical_max == doctest::Approx(15.0F));
  CHECK(kLeftIndicatorLampDefinition.byte_order ==
        vehicle_core::mazda_candidate::CandidateSignalDefinition::ByteOrder::Intel);
  CHECK(kFrontWiperDefinition.bit_length == 2);
  CHECK(kFrontWiperDefinition.dbc_start_bit == 21);
  CHECK(std::string{kFrontLeftDoorOpenRhdDefinition.name}.find("Reference") == std::string::npos);
}

TEST_CASE("ENGINE_DATA decodes the confirmed big-endian RPM vector") {
  // EngineRPM is 7|16@0+ (0.25,0) in the capture-derived DBC. SPEED remains
  // an out-of-scope candidate retained for existing consumers.
  const auto input = frame(0x202, 1000, {0x09, 0x5b, 0x00, 0x00, 0, 0, 0, 0});
  vehicle_core::VehicleState state{};

  CHECK(vehicle_core::mazda_candidate::decode_engine_data(input, state) ==
        vehicle_core::mazda_candidate::DecodeStatus::Updated);
  CHECK(state.engine_rpm.is_valid());
  CHECK(state.engine_rpm.value == doctest::Approx(598.75F));
  CHECK(state.speed_kph.is_valid());
  CHECK(state.speed_kph.value == doctest::Approx(0.0F));
  CHECK(state.timestamp_us == 1000);
}

TEST_CASE("ENGINE_DATA accepts representable boundaries and rejects invalid RPM") {
  vehicle_core::VehicleState state{};
  const auto boundary = frame(0x202, 10, {0x84, 0xd0, 0xff, 0xff, 0, 0, 0, 0});
  CHECK(vehicle_core::mazda_candidate::decode_engine_data(boundary, state) ==
        vehicle_core::mazda_candidate::DecodeStatus::Updated);
  CHECK(state.engine_rpm.value == doctest::Approx(8500.0F));
  CHECK(state.speed_kph.value == doctest::Approx(655.35F));

  const auto invalid = frame(0x202, 11, {0x84, 0xd1, 0, 0, 0, 0, 0, 0});
  CHECK(vehicle_core::mazda_candidate::decode_engine_data(invalid, state) ==
        vehicle_core::mazda_candidate::DecodeStatus::Invalid);
  CHECK(state.engine_rpm.value == doctest::Approx(8500.0F));
  CHECK(state.engine_rpm.last_update_us == 10);
}

TEST_CASE("candidate decoders reject wrong DLC, extended, remote, and other IDs") {
  vehicle_core::VehicleState state{};
  const auto short_engine = frame(0x202, 1, {0, 0, 0, 0, 0, 0, 0}, 7);
  CHECK(vehicle_core::mazda_candidate::decode(short_engine, state) ==
        vehicle_core::mazda_candidate::DecodeStatus::Invalid);
  CHECK(state.engine_rpm.is_unknown());
  const auto short_gear = frame(0x228, 1, {0x04, 0, 0, 0, 0, 0, 0}, 7);
  CHECK(vehicle_core::mazda_candidate::decode(short_gear, state) ==
        vehicle_core::mazda_candidate::DecodeStatus::Invalid);
  CHECK(state.selector_position.is_unknown());
  const auto short_doors = frame(0x43e, 1, {0, 0, 0, 0, 0, 0, 0}, 7);
  CHECK(vehicle_core::mazda_candidate::decode(short_doors, state) ==
        vehicle_core::mazda_candidate::DecodeStatus::Invalid);
  CHECK(state.liftgate_open.is_unknown());
  const auto short_blink = frame(0x09a, 1, {0, 0, 0, 0, 0, 0, 0}, 7);
  CHECK(vehicle_core::mazda_candidate::decode(short_blink, state) ==
        vehicle_core::mazda_candidate::DecodeStatus::Invalid);
  CHECK(state.left_indicator_lamp.is_unknown());

  auto extended = frame(0x202, 2, {0, 0, 0, 0, 0, 0, 0, 0});
  extended.identifier_format = vehicle_core::CanIdentifierFormat::Extended;
  CHECK(vehicle_core::mazda_candidate::decode(extended, state) ==
        vehicle_core::mazda_candidate::DecodeStatus::Ignored);
  auto remote = frame(0x202, 3, {0, 0, 0, 0, 0, 0, 0, 0});
  remote.remote_request = true;
  CHECK(vehicle_core::mazda_candidate::decode(remote, state) ==
        vehicle_core::mazda_candidate::DecodeStatus::Ignored);
  const auto other = frame(0x201, 4, {0, 0, 0, 0, 0, 0, 0, 0});
  CHECK(vehicle_core::mazda_candidate::decode(other, state) ==
        vehicle_core::mazda_candidate::DecodeStatus::Ignored);
}

TEST_CASE("GEAR keeps selector and actual transmission gear independent") {
  // Synthetic vector from the issue acceptance example: Drive, second gear.
  const auto input = frame(0x228, 2000, {0x24, 0x81, 0x07, 0xff, 0x04, 0xf0, 0, 0});
  vehicle_core::VehicleState state{};
  CHECK(vehicle_core::mazda_candidate::decode_gear(input, state) ==
        vehicle_core::mazda_candidate::DecodeStatus::Updated);
  CHECK(state.selector_position.value == vehicle_core::SelectorPosition::Drive);
  CHECK(state.actual_gear.value == vehicle_core::ActualGear::Second);
  CHECK(state.selector_position.last_update_us == 2000);
  CHECK(state.actual_gear.last_update_us == 2000);

  const auto reverse = frame(0x228, 2001, {0x02, 0, 0, 0, 0x1c, 0, 0, 0});
  REQUIRE(vehicle_core::mazda_candidate::decode_gear(reverse, state) ==
          vehicle_core::mazda_candidate::DecodeStatus::Updated);
  CHECK(state.selector_position.value == vehicle_core::SelectorPosition::Reverse);
  CHECK(state.actual_gear.value == vehicle_core::ActualGear::Reverse);
  CHECK(vehicle_core::mazda_candidate::kActualGearDefinition.physical_max >= 14.0F);
  const auto park = frame(0x228, 2002, {0x01, 0, 0, 0, 0x00, 0, 0, 0});
  REQUIRE(vehicle_core::mazda_candidate::decode_gear(park, state) ==
          vehicle_core::mazda_candidate::DecodeStatus::Updated);
  CHECK(state.selector_position.value == vehicle_core::SelectorPosition::Park);
  CHECK(state.actual_gear.value == vehicle_core::ActualGear::Park);

  const auto neutral = frame(0x228, 2003, {0x03, 0, 0, 0, 0x02, 0, 0, 0});
  REQUIRE(vehicle_core::mazda_candidate::decode_gear(neutral, state) ==
          vehicle_core::mazda_candidate::DecodeStatus::Updated);
  CHECK(state.selector_position.value == vehicle_core::SelectorPosition::Neutral);
  CHECK(state.actual_gear.value == vehicle_core::ActualGear::First);

  const std::array<std::uint8_t, 8> actual_raw{0, 1, 2, 3, 4, 5, 6, 14};
  const std::array<vehicle_core::ActualGear, 8> actual_expected{
      vehicle_core::ActualGear::ParkOrNeutral, vehicle_core::ActualGear::First,
      vehicle_core::ActualGear::Second,        vehicle_core::ActualGear::Third,
      vehicle_core::ActualGear::Fourth,        vehicle_core::ActualGear::Fifth,
      vehicle_core::ActualGear::Sixth,         vehicle_core::ActualGear::Reverse,
  };
  for (std::size_t index = 0; index < actual_raw.size(); ++index) {
    const auto actual =
        frame(0x228, 2010 + index,
              {0x01, 0, 0, 0, static_cast<std::uint8_t>(actual_raw[index] << 1U), 0, 0, 0});
    REQUIRE(vehicle_core::mazda_candidate::decode_gear(actual, state) ==
            vehicle_core::mazda_candidate::DecodeStatus::Updated);
    CHECK(state.actual_gear.value == actual_expected[index]);
  }
}

TEST_CASE("GEAR ignores shifting and undefined values without creating valid signals") {
  vehicle_core::VehicleState state{};
  const auto selector_only = frame(0x228, 1, {0x04, 0, 0, 0, 0x1e, 0, 0, 0});
  CHECK(vehicle_core::mazda_candidate::decode_gear(selector_only, state) ==
        vehicle_core::mazda_candidate::DecodeStatus::Updated);
  CHECK(state.selector_position.value == vehicle_core::SelectorPosition::Drive);
  CHECK(state.actual_gear.is_unknown());

  const auto unknown = frame(0x228, 1, {0x00, 0, 0, 0, 0x1e, 0, 0, 0});
  CHECK(vehicle_core::mazda_candidate::decode_gear(unknown, state) ==
        vehicle_core::mazda_candidate::DecodeStatus::Invalid);
  CHECK(state.selector_position.value == vehicle_core::SelectorPosition::Drive);
  CHECK(state.actual_gear.is_unknown());

  const auto undefined = frame(0x228, 2, {0x05, 0, 0, 0, 0x0e, 0, 0, 0});
  CHECK(vehicle_core::mazda_candidate::decode_gear(undefined, state) ==
        vehicle_core::mazda_candidate::DecodeStatus::Invalid);
  CHECK(state.selector_position.value == vehicle_core::SelectorPosition::Drive);
  CHECK(state.actual_gear.is_unknown());
}

TEST_CASE("DOORS decodes every confirmed boolean with the supplied DBC bit layout") {
  using namespace vehicle_core;
  using namespace vehicle_core::mazda_candidate;

  VehicleState state{};
  const auto closed = frame(kDoorsId, 100, {0, 0, 0, 0, 0, 0, 0, 0});
  REQUIRE(decode_doors(closed, state) == DecodeStatus::Updated);
  CHECK_FALSE(state.liftgate_open.value);
  CHECK_FALSE(state.rear_right_door_open.value);
  CHECK_FALSE(state.rear_left_door_open.value);
  CHECK_FALSE(state.front_left_door_open_rhd.value);
  CHECK_FALSE(state.front_right_door_open_rhd.value);
  CHECK_FALSE(state.doors_unlocked.value);

  struct DoorBitTest {
    std::uint8_t byte3_mask;
    std::uint8_t byte4_mask;
    Signal<bool> VehicleState::*signal;
  };
  const std::array<DoorBitTest, 6> door_bits{{
      {0, 0x01, &VehicleState::liftgate_open},
      {0, 0x04, &VehicleState::rear_right_door_open},
      {0, 0x08, &VehicleState::rear_left_door_open},
      {0, 0x10, &VehicleState::front_left_door_open_rhd},
      {0, 0x20, &VehicleState::front_right_door_open_rhd},
      {0x40, 0, &VehicleState::doors_unlocked},
  }};
  for (std::size_t index = 0; index < door_bits.size(); ++index) {
    VehicleState one_bit_state{};
    const auto one_bit =
        frame(kDoorsId, 110 + index,
              {0, 0, 0, door_bits[index].byte3_mask, door_bits[index].byte4_mask, 0, 0, 0});
    REQUIRE(decode_doors(one_bit, one_bit_state) == DecodeStatus::Updated);
    CHECK((one_bit_state.*door_bits[index].signal).value);
  }

  // D4 bit 6 is DoorsUnlocked; D5 bits 0, 2, 3, 4, and 5 are the five
  // confirmed door/liftgate fields.
  const auto open = frame(kDoorsId, 200, {0, 0, 0, 0x40, 0x3d, 0, 0, 0});
  REQUIRE(decode_doors(open, state) == DecodeStatus::Updated);
  CHECK(state.liftgate_open.value);
  CHECK(state.rear_right_door_open.value);
  CHECK(state.rear_left_door_open.value);
  CHECK(state.front_left_door_open_rhd.value);
  CHECK(state.front_right_door_open_rhd.value);
  CHECK(state.doors_unlocked.value);
  CHECK(state.timestamp_us == 200);
  CHECK(state.liftgate_open.last_update_us == 200);
}

TEST_CASE("BLINK_INFO decodes indicator lamps and low-speed wiper status") {
  using namespace vehicle_core;
  using namespace vehicle_core::mazda_candidate;

  VehicleState state{};
  const auto off = frame(kBlinkInfoId, 300, {0, 0, 0, 0, 0, 0, 0, 0});
  REQUIRE(decode_blink_info(off, state) == DecodeStatus::Updated);
  CHECK_FALSE(state.left_indicator_lamp.value);
  CHECK_FALSE(state.right_indicator_lamp.value);
  CHECK_FALSE(state.wiper_low.value);

  struct BlinkBitTest {
    std::uint8_t byte2_mask;
    std::uint8_t byte4_mask;
    Signal<bool> VehicleState::*signal;
  };
  const std::array<BlinkBitTest, 3> blink_bits{{
      {0x04, 0, &VehicleState::left_indicator_lamp},
      {0x08, 0, &VehicleState::right_indicator_lamp},
      {0, 0x02, &VehicleState::wiper_low},
  }};
  for (std::size_t index = 0; index < blink_bits.size(); ++index) {
    VehicleState one_bit_state{};
    const auto one_bit =
        frame(kBlinkInfoId, 310 + index,
              {0, 0, blink_bits[index].byte2_mask, 0, blink_bits[index].byte4_mask, 0, 0, 0});
    REQUIRE(decode_blink_info(one_bit, one_bit_state) == DecodeStatus::Updated);
    CHECK((one_bit_state.*blink_bits[index].signal).value);
  }

  // D3 bits 2 and 3 are the left/right lamps; D5 bit 1 is WiperLow.
  const auto on = frame(kBlinkInfoId, 400, {0, 0, 0x0c, 0, 0x02, 0, 0, 0});
  REQUIRE(decode_blink_info(on, state) == DecodeStatus::Updated);
  CHECK(state.left_indicator_lamp.value);
  CHECK(state.right_indicator_lamp.value);
  CHECK(state.wiper_low.value);
  CHECK(state.timestamp_us == 400);
  CHECK(state.right_indicator_lamp.last_update_us == 400);

  const auto late = frame(kBlinkInfoId, 399, {0, 0, 0, 0, 0, 0, 0, 0});
  CHECK(decode_blink_info(late, state) == DecodeStatus::Invalid);
  CHECK(state.left_indicator_lamp.value);
  CHECK(state.right_indicator_lamp.value);
  CHECK(state.wiper_low.value);
}

TEST_CASE("TURN_SWITCH decodes all front-wiper enumeration values") {
  using namespace vehicle_core;
  using namespace vehicle_core::mazda_candidate;

  VehicleState state{};
  const std::array<FrontWiperPosition, 4> expected{
      FrontWiperPosition::Off,
      FrontWiperPosition::On,
      FrontWiperPosition::High,
      FrontWiperPosition::Intermittent,
  };
  for (std::uint8_t raw = 0; raw < expected.size(); ++raw) {
    const auto input =
        frame(kTurnSwitchId, 500 + raw, {0, 0, static_cast<std::uint8_t>(raw << 4), 0, 0, 0, 0, 0});
    REQUIRE(decode_turn_switch(input, state) == DecodeStatus::Updated);
    CHECK(state.front_wiper.value == expected[raw]);
    CHECK(state.front_wiper.last_update_us == 500 + raw);
  }
}

TEST_CASE("invalid and missing updates become stale on the same replay clock") {
  using namespace vehicle_core::mazda_candidate;
  test_support::FakeClock clock;
  test_support::DirectFrameFeeder feeder;
  vehicle_core::VehicleFreshnessPolicy policy{};
  policy.speed_kph_timeout_us = 100;
  policy.engine_rpm_timeout_us = 100;
  policy.selector_position_timeout_us = 100;
  policy.actual_gear_timeout_us = 100;
  vehicle_core::VehicleStateStore store{clock, policy};
  feeder.feed(frame(kEngineDataId, 1000, {0x09, 0x5b, 0, 0, 0, 0, 0, 0}),
              [&](const vehicle_core::RawCanFrame &value) {
                clock.set(value.timestamp_us);
                (void)vehicle_core::mazda_candidate::decode(value, store.mutable_state());
              });
  feeder.feed(frame(kGearId, 1100, {0x24, 0x81, 0x07, 0xff, 0x04, 0xf0, 0, 0}),
              [&](const vehicle_core::RawCanFrame &value) {
                clock.set(value.timestamp_us);
                (void)vehicle_core::mazda_candidate::decode(value, store.mutable_state());
              });
  // A wrong-DLC frame is delivered directly and cannot replace accepted data.
  feeder.feed(frame(kEngineDataId, 1150, {0x84, 0xd1, 0, 0, 0, 0, 0}, 7),
              [&](const vehicle_core::RawCanFrame &value) {
                clock.set(value.timestamp_us);
                (void)vehicle_core::mazda_candidate::decode(value, store.mutable_state());
              });
  // Match ReplayHarness::replay's final advance: the test clock reaches the
  // end of the synthetic stream after all frames have been delivered.
  clock.set(std::numeric_limits<vehicle_core::MonotonicTimestamp>::max());
  REQUIRE(store.state().engine_rpm.is_valid());
  REQUIRE(store.state().actual_gear.is_valid());
  const auto stale = store.snapshot();
  CHECK(stale.engine_rpm.is_stale());
  CHECK(stale.speed_kph.is_stale());
  CHECK(stale.selector_position.is_stale());
  CHECK(stale.actual_gear.is_stale());
  CHECK(stale.engine_rpm.value == doctest::Approx(598.75F));
}
