#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <string>

#include "raw_capture/replay.hpp"
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

TEST_CASE("candidate definitions retain source provenance and unspecified timing") {
  using namespace vehicle_core::mazda_candidate;
  CHECK(kEngineDataDefinition.identifier == 0x202);
  CHECK(kEngineDataDefinition.expected_dlc == 8);
  CHECK_FALSE(kEngineDataDefinition.expected_period_us.has_value());
  CHECK_FALSE(kEngineDataDefinition.freshness_timeout_us.has_value());
  CHECK(kEngineDataDefinition.pending_validation);
  CHECK(std::string{kEngineDataDefinition.provenance}.find("95f3d52f") != std::string::npos);
  CHECK(kEngineRpmDefinition.scale == doctest::Approx(0.25F));
  CHECK(kEngineSpeedDefinition.scale == doctest::Approx(0.01F));
  CHECK(kEngineRpmDefinition.unit == vehicle_core::SignalUnit::RevolutionsPerMinute);
  CHECK(kEngineSpeedDefinition.unit == vehicle_core::SignalUnit::KilometresPerHour);
}

TEST_CASE("ENGINE_DATA decodes upstream-derived big-endian golden vector") {
  // Derived from mazda_2017.dbc: RPM 7|16@0+ (0.25,0), SPEED 23|16@0+
  // (0.01,0), source pinned in docs/development/mcan-10-opendbc-signal-evidence.md.
  const auto input = frame(0x202, 1000, {0x09, 0x5b, 0x00, 0x00, 0, 0, 0, 0});
  vehicle_core::VehicleState state{};

  CHECK(vehicle_core::mazda_candidate::decode_engine_data(
            input, state) == vehicle_core::mazda_candidate::DecodeStatus::Updated);
  CHECK(state.engine_rpm.is_valid());
  CHECK(state.engine_rpm.value == doctest::Approx(598.75F));
  CHECK(state.speed_kph.is_valid());
  CHECK(state.speed_kph.value == doctest::Approx(0.0F));
  CHECK(state.timestamp_us == 1000);
}

TEST_CASE("ENGINE_DATA accepts representable boundaries and rejects invalid RPM") {
  vehicle_core::VehicleState state{};
  const auto boundary = frame(0x202, 10, {0x84, 0xd0, 0xff, 0xff, 0, 0, 0, 0});
  CHECK(vehicle_core::mazda_candidate::decode_engine_data(
            boundary, state) == vehicle_core::mazda_candidate::DecodeStatus::Updated);
  CHECK(state.engine_rpm.value == doctest::Approx(8500.0F));
  CHECK(state.speed_kph.value == doctest::Approx(655.35F));

  const auto invalid = frame(0x202, 11, {0x84, 0xd1, 0, 0, 0, 0, 0, 0});
  CHECK(vehicle_core::mazda_candidate::decode_engine_data(
            invalid, state) == vehicle_core::mazda_candidate::DecodeStatus::Invalid);
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
  CHECK(vehicle_core::mazda_candidate::decode_gear(
            input, state) == vehicle_core::mazda_candidate::DecodeStatus::Updated);
  CHECK(state.selector_position.value == vehicle_core::SelectorPosition::Drive);
  CHECK(state.actual_gear.value == vehicle_core::ActualGear::Second);
  CHECK(state.selector_position.last_update_us == 2000);
  CHECK(state.actual_gear.last_update_us == 2000);

  const auto reverse = frame(0x228, 2001, {0x02, 0, 0, 0, 0x1c, 0, 0, 0});
  REQUIRE(vehicle_core::mazda_candidate::decode_gear(reverse, state) ==
          vehicle_core::mazda_candidate::DecodeStatus::Updated);
  CHECK(state.selector_position.value == vehicle_core::SelectorPosition::Reverse);
  CHECK(state.actual_gear.value == vehicle_core::ActualGear::Reverse);
  const auto park = frame(0x228, 2002, {0x01, 0, 0, 0, 0x00, 0, 0, 0});
  REQUIRE(vehicle_core::mazda_candidate::decode_gear(park, state) ==
          vehicle_core::mazda_candidate::DecodeStatus::Updated);
  CHECK(state.selector_position.value == vehicle_core::SelectorPosition::Park);
  CHECK(state.actual_gear.value == vehicle_core::ActualGear::Park);
}

TEST_CASE("GEAR ignores shifting and undefined values without creating valid signals") {
  vehicle_core::VehicleState state{};
  const auto selector_only = frame(0x228, 1, {0x04, 0, 0, 0, 0x1e, 0, 0, 0});
  CHECK(vehicle_core::mazda_candidate::decode_gear(
            selector_only, state) == vehicle_core::mazda_candidate::DecodeStatus::Updated);
  CHECK(state.selector_position.value == vehicle_core::SelectorPosition::Drive);
  CHECK(state.actual_gear.is_unknown());

  const auto unknown = frame(0x228, 1, {0x00, 0, 0, 0, 0x1e, 0, 0, 0});
  CHECK(vehicle_core::mazda_candidate::decode_gear(
            unknown, state) == vehicle_core::mazda_candidate::DecodeStatus::Invalid);
  CHECK(state.selector_position.value == vehicle_core::SelectorPosition::Drive);
  CHECK(state.actual_gear.is_unknown());

  const auto undefined = frame(0x228, 2, {0x05, 0, 0, 0, 0x0e, 0, 0, 0});
  CHECK(vehicle_core::mazda_candidate::decode_gear(
            undefined, state) == vehicle_core::mazda_candidate::DecodeStatus::Invalid);
  CHECK(state.selector_position.is_unknown());
  CHECK(state.actual_gear.is_unknown());
}

TEST_CASE("invalid and missing updates become stale on the same replay clock") {
  const std::string capture =
      "MCAN-CAPTURE 1\n"
      "SESSION firmware=test board=host bitrate_bps=500000 clock=monotonic clock_unit=us "
      "byte_order=big-endian clock_hz=1000000 dropped_frames=0 dropped_records=0\n"
      "FRAME t_us=1000 bus=0 id=0x202 format=std rtr=0 dlc=8 data=095b000000000000\n"
      "FRAME t_us=1100 bus=0 id=0x228 format=std rtr=0 dlc=8 data=248107ff04f00000\n"
      "FRAME t_us=1150 bus=0 id=0x202 format=std rtr=0 dlc=7 data=84d10000000000\n";
  const auto parsed = raw_capture::CaptureReader{}.read(capture);
  REQUIRE(parsed.ok);
  raw_capture::SimulatedMonotonicClock clock;
  raw_capture::ReplayHarness replay{clock};
  vehicle_core::VehicleFreshnessPolicy policy{};
  policy.speed_kph_timeout_us = 100;
  policy.engine_rpm_timeout_us = 100;
  policy.selector_position_timeout_us = 100;
  policy.actual_gear_timeout_us = 100;
  vehicle_core::VehicleStateStore store{clock, policy};
  replay.replay(parsed.records, [&](const vehicle_core::RawCanFrame &value,
                                    raw_capture::SimulatedMonotonicClock &) {
    (void)vehicle_core::mazda_candidate::decode(value, store.mutable_state());
  });
  REQUIRE(store.state().engine_rpm.is_valid());
  REQUIRE(store.state().actual_gear.is_valid());
  const auto stale = store.snapshot();
  CHECK(stale.engine_rpm.is_stale());
  CHECK(stale.speed_kph.is_stale());
  CHECK(stale.selector_position.is_stale());
  CHECK(stale.actual_gear.is_stale());
  CHECK(stale.engine_rpm.value == doctest::Approx(598.75F));
}
