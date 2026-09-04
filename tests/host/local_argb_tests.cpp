#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <vector>

#include "local_argb/local_argb.h"

namespace {

class FakeSink final : public local_argb::PixelSink {
public:
  bool write(const local_argb::Rgb color) noexcept override {
    attempts.push_back(color);
    if (failures_remaining > 0) {
      --failures_remaining;
      return false;
    }
    return true;
  }

  std::vector<local_argb::Rgb> attempts;
  int failures_remaining{0};
};

local_argb::SemanticSnapshot valid(const vehicle_core::TurnState turn,
                                   const vehicle_core::MonotonicTimestamp timestamp = 100) {
  return {turn, vehicle_core::SignalStatus::Valid, timestamp, local_argb::SemanticHealth::Online};
}

} // namespace

TEST_CASE("startup transmits an explicit black frame") {
  FakeSink sink;
  local_argb::Controller controller{sink};
  CHECK(controller.start());
  REQUIRE(sink.attempts.size() == 1);
  CHECK(sink.attempts[0] == local_argb::kBlack);
}

TEST_CASE("semantic states map to bounded diagnostic colors") {
  CHECK(local_argb::color_for(valid(vehicle_core::TurnState::Left), 100) == local_argb::kLeftGreen);
  CHECK(local_argb::color_for(valid(vehicle_core::TurnState::Right), 100) ==
        local_argb::kRightBlue);
  CHECK(local_argb::color_for(valid(vehicle_core::TurnState::Hazard), 100) ==
        local_argb::kHazardAmber);
  CHECK(local_argb::color_for(valid(vehicle_core::TurnState::Off), 100) == local_argb::kBlack);

  const local_argb::Rgb colors[]{local_argb::kBlack, local_argb::kLeftGreen, local_argb::kRightBlue,
                                 local_argb::kHazardAmber};
  for (const auto color : colors) {
    CHECK(color.red <= local_argb::kBrightnessCeiling);
    CHECK(color.green <= local_argb::kBrightnessCeiling);
    CHECK(color.blue <= local_argb::kBrightnessCeiling);
  }
}

TEST_CASE("unknown stale offline and decoder error fail black") {
  auto snapshot = valid(vehicle_core::TurnState::Left);
  snapshot.turn_status = vehicle_core::SignalStatus::Unknown;
  CHECK(local_argb::color_for(snapshot, 100) == local_argb::kBlack);
  snapshot.turn_status = vehicle_core::SignalStatus::Stale;
  CHECK(local_argb::color_for(snapshot, 100) == local_argb::kBlack);
  snapshot.turn_status = vehicle_core::SignalStatus::Valid;
  snapshot.health = local_argb::SemanticHealth::CanOffline;
  CHECK(local_argb::color_for(snapshot, 100) == local_argb::kBlack);
  snapshot.health = local_argb::SemanticHealth::DecoderError;
  CHECK(local_argb::color_for(snapshot, 100) == local_argb::kBlack);
}

TEST_CASE("freshness is inclusive at 250000 us and clears at 250001 us") {
  const auto snapshot = valid(vehicle_core::TurnState::Left, 1'000);
  CHECK(local_argb::color_for(snapshot, 251'000) == local_argb::kLeftGreen);
  CHECK(local_argb::color_for(snapshot, 251'001) == local_argb::kBlack);
}

TEST_CASE("controller independently clears stale state and recovers same direction") {
  FakeSink sink;
  local_argb::Controller controller{sink};
  REQUIRE(controller.start());
  REQUIRE(controller.apply(valid(vehicle_core::TurnState::Left, 10), 10));
  REQUIRE(controller.tick(250'011));
  CHECK(sink.attempts.back() == local_argb::kBlack);
  REQUIRE(controller.apply(valid(vehicle_core::TurnState::Left, 300'000), 300'000));
  CHECK(sink.attempts.back() == local_argb::kLeftGreen);
}

TEST_CASE("duplicate semantic submissions avoid redundant hardware refreshes") {
  FakeSink sink;
  local_argb::Controller controller{sink};
  REQUIRE(controller.start());
  const auto snapshot = valid(vehicle_core::TurnState::Right, 50);
  REQUIRE(controller.apply(snapshot, 50));
  const auto writes = sink.attempts.size();
  REQUIRE(controller.apply(snapshot, 60));
  REQUIRE(controller.tick(70));
  CHECK(sink.attempts.size() == writes);
}

TEST_CASE("length-one mailbox overwrites backpressure with newest semantics") {
  local_argb::Mailbox mailbox;
  mailbox.submit(valid(vehicle_core::TurnState::Left, 1));
  mailbox.submit(valid(vehicle_core::TurnState::Hazard, 2));
  local_argb::SemanticSnapshot result{};
  REQUIRE(mailbox.take(result));
  CHECK(result.turn == vehicle_core::TurnState::Hazard);
  CHECK(result.turn_last_update_us == 2);
  CHECK_FALSE(mailbox.take(result));
}

TEST_CASE("driver failure attempts black and retries only after recovery input") {
  FakeSink sink;
  local_argb::Controller controller{sink};
  REQUIRE(controller.start());
  sink.failures_remaining = 1;
  CHECK_FALSE(controller.apply(valid(vehicle_core::TurnState::Left), 100));
  REQUIRE(sink.attempts.size() == 3);
  CHECK(sink.attempts[1] == local_argb::kLeftGreen);
  CHECK(sink.attempts[2] == local_argb::kBlack);
  CHECK(controller.faulted());
  REQUIRE(controller.tick(110));
  CHECK(sink.attempts.size() == 3);
  REQUIRE(controller.apply(valid(vehicle_core::TurnState::Left, 120), 120));
  CHECK(sink.attempts.back() == local_argb::kLeftGreen);
}

TEST_CASE("failed startup clear is retried until black succeeds") {
  FakeSink sink;
  sink.failures_remaining = 1;
  local_argb::Controller controller{sink};
  CHECK_FALSE(controller.start());
  CHECK(controller.faulted());
  CHECK(controller.tick(1));
  REQUIRE(sink.attempts.size() == 2);
  CHECK(std::all_of(sink.attempts.begin(), sink.attempts.end(),
                    [](const local_argb::Rgb color) { return color == local_argb::kBlack; }));
}

TEST_CASE("failed fallback clear blocks color until black succeeds") {
  FakeSink sink;
  local_argb::Controller controller{sink};
  REQUIRE(controller.start());
  sink.failures_remaining = 2;
  CHECK_FALSE(controller.apply(valid(vehicle_core::TurnState::Left), 100));
  CHECK(controller.faulted());
  REQUIRE(controller.apply(valid(vehicle_core::TurnState::Right, 110), 110));
  CHECK(sink.attempts.back() == local_argb::kBlack);
  CHECK(controller.faulted());
  REQUIRE(controller.apply(valid(vehicle_core::TurnState::Right, 120), 120));
  CHECK(sink.attempts.back() == local_argb::kRightBlue);
  CHECK_FALSE(controller.faulted());
}
