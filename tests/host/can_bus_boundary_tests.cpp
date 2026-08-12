#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <type_traits>

#include "can_bus/can_bus.h"
#include "can_bus/configuration.hpp"
#include "can_bus/frame_ring.hpp"

namespace {

vehicle_core::RawCanFrame make_frame(const std::uint32_t identifier,
                                     const vehicle_core::MonotonicTimestamp timestamp) {
  vehicle_core::RawCanFrame frame{};
  frame.timestamp_us = timestamp;
  frame.bus_id = 2;
  frame.identifier = identifier;
  frame.identifier_format = vehicle_core::CanIdentifierFormat::Extended;
  frame.remote_request = true;
  frame.dlc = 8;
  frame.data = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
  return frame;
}

} // namespace

TEST_CASE("configuration permits only explicit classic CAN bitrates") {
  CHECK(can_bus::internal::is_configuration_valid({125'000, 0}));
  CHECK(can_bus::internal::is_configuration_valid({250'000, 0}));
  CHECK(can_bus::internal::is_configuration_valid({500'000, 0}));
  CHECK(can_bus::internal::is_configuration_valid({1'000'000, 0}));
  CHECK_FALSE(can_bus::internal::is_configuration_valid({0, 0}));
  CHECK_FALSE(can_bus::internal::is_configuration_valid({100'000, 0}));
  CHECK_FALSE(can_bus::internal::is_configuration_valid({499'999, 0}));
}

TEST_CASE("frame boundary preserves every receive field") {
  can_bus::internal::FrameRing<4> ring;
  const auto input = make_frame(0x1abcdef0, 123'456);
  REQUIRE(ring.push(input));

  vehicle_core::RawCanFrame output{};
  REQUIRE(ring.pop(output));
  CHECK(output.timestamp_us == input.timestamp_us);
  CHECK(output.bus_id == input.bus_id);
  CHECK(output.identifier == input.identifier);
  CHECK(output.identifier_format == input.identifier_format);
  CHECK(output.remote_request == input.remote_request);
  CHECK(output.dlc == input.dlc);
  CHECK(output.data == input.data);
}

TEST_CASE("frame boundary distinguishes standard data and extended RTR frames") {
  can_bus::internal::FrameRing<4> ring;
  auto standard = make_frame(0x321, 10);
  standard.identifier_format = vehicle_core::CanIdentifierFormat::Standard;
  standard.remote_request = false;
  standard.dlc = 3;
  standard.data = {0xa5, 0x5a, 0xc3, 0, 0, 0, 0, 0};
  const auto extended_rtr = make_frame(0x1abcde0, 20);
  REQUIRE(ring.push(standard));
  REQUIRE(ring.push(extended_rtr));

  vehicle_core::RawCanFrame output{};
  REQUIRE(ring.pop(output));
  CHECK(output.identifier == 0x321);
  CHECK_FALSE(output.is_extended());
  CHECK_FALSE(output.remote_request);
  CHECK(output.dlc == 3);
  CHECK(output.data[0] == 0xa5);
  CHECK(output.data[1] == 0x5a);
  CHECK(output.data[2] == 0xc3);

  REQUIRE(ring.pop(output));
  CHECK(output.identifier == 0x1abcde0);
  CHECK(output.is_extended());
  CHECK(output.remote_request);
  CHECK(output.dlc == 8);
}

TEST_CASE("frame boundary is FIFO") {
  can_bus::internal::FrameRing<3> ring;
  REQUIRE(ring.push(make_frame(1, 10)));
  REQUIRE(ring.push(make_frame(2, 20)));
  REQUIRE(ring.push(make_frame(3, 30)));

  vehicle_core::RawCanFrame output{};
  REQUIRE(ring.pop(output));
  CHECK(output.identifier == 1);
  REQUIRE(ring.pop(output));
  CHECK(output.identifier == 2);
  REQUIRE(ring.pop(output));
  CHECK(output.identifier == 3);
  CHECK_FALSE(ring.pop(output));
}

TEST_CASE("full boundary drops newest without disturbing queued frames") {
  can_bus::internal::FrameRing<2> ring;
  REQUIRE(ring.push(make_frame(1, 10)));
  REQUIRE(ring.push(make_frame(2, 20)));
  CHECK_FALSE(ring.push(make_frame(3, 30)));

  const auto stats = ring.snapshot(can_bus::StatisticsOperation::kSnapshot);
  CHECK(stats.frames_received == 3);
  CHECK(stats.frames_queued == 2);
  CHECK(stats.frames_dropped == 1);
  CHECK(stats.queue_overflows == 1);
  CHECK(stats.queue_depth == 2);
  CHECK(stats.queue_high_watermark == 2);
  CHECK(stats.queue_capacity == 2);

  vehicle_core::RawCanFrame output{};
  REQUIRE(ring.pop(output));
  CHECK(output.identifier == 1);
  REQUIRE(ring.pop(output));
  CHECK(output.identifier == 2);
}

TEST_CASE("absent and slow consumers do not prevent later producer calls") {
  can_bus::internal::FrameRing<4> ring;
  for (std::uint32_t id = 0; id < 1000; ++id) {
    (void)ring.push(make_frame(id, id));
  }
  const auto full = ring.snapshot(can_bus::StatisticsOperation::kSnapshot);
  CHECK(full.frames_received == 1000);
  CHECK(full.frames_queued == 4);
  CHECK(full.frames_dropped == 996);

  vehicle_core::RawCanFrame output{};
  REQUIRE(ring.pop(output));
  CHECK(output.identifier == 0);
  REQUIRE(ring.push(make_frame(1000, 1000)));
  CHECK(ring.snapshot(can_bus::StatisticsOperation::kSnapshot).queue_depth == 4);
}

TEST_CASE("statistics reset preserves queue and starts watermark at current depth") {
  can_bus::internal::FrameRing<4> ring;
  REQUIRE(ring.push(make_frame(1, 10)));
  REQUIRE(ring.push(make_frame(2, 20)));
  ring.record_bus_error(3);
  ring.record_driver_rx_missed(4);
  ring.record_controller_reset();

  const auto before = ring.snapshot(can_bus::StatisticsOperation::kSnapshotAndReset);
  CHECK(before.frames_received == 2);
  CHECK(before.bus_errors == 3);
  CHECK(before.driver_rx_missed == 4);
  CHECK(before.controller_resets == 1);
  CHECK(before.queue_depth == 2);

  const auto reset = ring.snapshot(can_bus::StatisticsOperation::kSnapshot);
  CHECK(reset.frames_received == 0);
  CHECK(reset.frames_queued == 0);
  CHECK(reset.frames_delivered == 0);
  CHECK(reset.frames_dropped == 0);
  CHECK(reset.bus_errors == 0);
  CHECK(reset.driver_rx_missed == 0);
  CHECK(reset.controller_resets == 0);
  CHECK(reset.queue_depth == 2);
  CHECK(reset.queue_high_watermark == 2);

  vehicle_core::RawCanFrame output{};
  REQUIRE(ring.pop(output));
  CHECK(output.identifier == 1);
  REQUIRE(ring.pop(output));
  CHECK(output.identifier == 2);
}

TEST_CASE("acquisition boundary has fixed storage and value semantics") {
  CHECK(std::is_trivially_copyable_v<vehicle_core::RawCanFrame>);
  CHECK(can_bus::kQueueCapacity == 64);
  CHECK(sizeof(can_bus::internal::FrameRing<4>) < 512);
}
