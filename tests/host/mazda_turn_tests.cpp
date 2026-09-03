#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <vector>

#include "raw_capture/replay.hpp"
#include "vehicle_core/vehicle_core.hpp"

namespace {

vehicle_core::RawCanFrame turn_frame(const vehicle_core::MonotonicTimestamp timestamp,
                                     const bool hazard, const bool left, const bool right) {
  vehicle_core::RawCanFrame frame{};
  frame.timestamp_us = timestamp;
  frame.identifier = vehicle_core::mazda_candidate::kTurnSwitchId;
  frame.dlc = 8;
  frame.data[1] = static_cast<std::uint8_t>((hazard ? 1U << 2U : 0U) | (right ? 1U << 4U : 0U) |
                                            (left ? 1U << 5U : 0U));
  return frame;
}

} // namespace

TEST_CASE("turn switch normalizes candidate bits with hazard and conflict precedence") {
  using namespace vehicle_core;
  using namespace vehicle_core::mazda_candidate;

  VehicleState state{};
  CHECK(decode_turn_switch(turn_frame(0, false, false, false), state) == DecodeStatus::Updated);
  CHECK(state.turn_state.value == TurnState::Off);
  CHECK(state.effective_turn_state() == TurnState::Off);

  CHECK(decode_turn_switch(turn_frame(1, false, true, false), state) == DecodeStatus::Updated);
  CHECK(state.turn_state.value == TurnState::Left);
  CHECK(decode_turn_switch(turn_frame(2, false, false, true), state) == DecodeStatus::Updated);
  CHECK(state.turn_state.value == TurnState::Right);
  CHECK(decode_turn_switch(turn_frame(3, false, true, true), state) == DecodeStatus::Updated);
  CHECK(state.turn_state.value == TurnState::Unknown);
  CHECK(state.effective_turn_state() == TurnState::Unknown);
  CHECK(decode_turn_switch(turn_frame(4, true, true, true), state) == DecodeStatus::Updated);
  CHECK(state.turn_state.value == TurnState::Hazard);
  CHECK(state.hazard_request.value);
  CHECK(state.left_turn_request.value);
  CHECK(state.right_turn_request.value);
}

TEST_CASE("turn switch rejects malformed input and ignores diagnostic blink info") {
  using namespace vehicle_core;
  using namespace vehicle_core::mazda_candidate;

  VehicleState state{};
  const auto initial = turn_frame(100, false, true, false);
  REQUIRE(decode_turn_switch(initial, state) == DecodeStatus::Updated);

  auto short_frame = initial;
  short_frame.dlc = 7;
  CHECK(decode_turn_switch(short_frame, state) == DecodeStatus::Invalid);
  CHECK(state.turn_state.value == TurnState::Left);

  auto extended = initial;
  extended.identifier_format = CanIdentifierFormat::Extended;
  CHECK(decode_turn_switch(extended, state) == DecodeStatus::Ignored);

  auto remote = initial;
  remote.remote_request = true;
  CHECK(decode_turn_switch(remote, state) == DecodeStatus::Ignored);

  auto invalid_identifier = initial;
  invalid_identifier.identifier = 0x800;
  CHECK(decode_turn_switch(invalid_identifier, state) == DecodeStatus::Ignored);

  auto blink_info = initial;
  blink_info.identifier = kBlinkInfoId;
  blink_info.data[1] = 0xff;
  CHECK(decode(blink_info, state) == DecodeStatus::Ignored);
  CHECK(state.turn_state.value == TurnState::Left);
  CHECK(state.left_turn_request.value);
}

TEST_CASE("duplicate turn states do not create duplicate semantic edges") {
  vehicle_core::VehicleState state{};

  const auto first = state.update_turn(vehicle_core::TurnState::Left, 10);
  REQUIRE(first.has_value());
  CHECK(first->previous == vehicle_core::TurnState::Unknown);
  CHECK(first->current == vehicle_core::TurnState::Left);
  CHECK_FALSE(state.update_turn(vehicle_core::TurnState::Left, 11).has_value());

  const auto changed = state.update_turn(vehicle_core::TurnState::Hazard, 20);
  REQUIRE(changed.has_value());
  CHECK(changed->previous == vehicle_core::TurnState::Left);
  CHECK(changed->current == vehicle_core::TurnState::Hazard);
  CHECK_FALSE(state.update_turn(vehicle_core::TurnState::Hazard, 21).has_value());
}

TEST_CASE("simulated replay makes turn stale after 250 ms and recovery actionable") {
  using namespace vehicle_core;
  using namespace vehicle_core::mazda_candidate;

  raw_capture::SimulatedMonotonicClock clock;
  raw_capture::ReplayHarness replay{clock};
  VehicleStateStore store{clock};
  CHECK(store.snapshot().effective_turn_state() == TurnState::Unknown);
  replay.load({raw_capture::CaptureRecord::frame_record(turn_frame(1'000, false, true, false)),
               raw_capture::CaptureRecord::frame_record(turn_frame(301'000, false, false, true))});

  CHECK(replay.advance_to(1'000, [&](const RawCanFrame &frame, auto &) {
    CHECK(decode_turn_switch(frame, store.mutable_state()) == DecodeStatus::Updated);
  }) == 1);
  CHECK(store.snapshot().turn_state.is_valid());
  CHECK(store.snapshot().effective_turn_state() == TurnState::Left);

  CHECK(store.state().snapshot(251'000).turn_state.is_valid());
  CHECK(replay.advance_to(251'001, [&](const RawCanFrame &, auto &) {}) == 0);
  const auto stale = store.snapshot();
  CHECK(stale.turn_state.is_stale());
  CHECK(stale.effective_turn_state() == TurnState::Unknown);

  CHECK(replay.advance_to(301'000, [&](const RawCanFrame &frame, auto &) {
    CHECK(decode(frame, store.mutable_state()) == DecodeStatus::Updated);
  }) == 1);
  const auto recovered = store.snapshot();
  CHECK(recovered.turn_state.is_valid());
  CHECK(recovered.effective_turn_state() == TurnState::Right);
  CHECK(recovered.timestamp_us == 301'000);
}

TEST_CASE("candidate definitions document provenance and diagnostic-only phase") {
  using namespace vehicle_core::mazda_candidate;
  CHECK(kTurnSwitchDefinition.identifier == 0x091);
  CHECK(kTurnSwitchDefinition.expected_dlc == 8);
  CHECK(kTurnSwitchDefinition.freshness_timeout_us.value() == 250'000);
  CHECK(kTurnSwitchDefinition.pending_validation);
  CHECK(kBlinkInfoDefinition.identifier == 0x09a);
  CHECK_FALSE(kBlinkInfoDefinition.freshness_timeout_us.has_value());
  CHECK(kBlinkInfoDefinition.pending_validation);
  CHECK(kHazardDefinition.start_bit == 10);
  CHECK(kTurnRightSwitchDefinition.start_bit == 12);
  CHECK(kTurnLeftSwitchDefinition.start_bit == 13);
}
