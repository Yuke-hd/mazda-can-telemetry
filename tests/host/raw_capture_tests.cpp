#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string>
#include <string_view>
#include <vector>

#include "raw_capture/raw_capture.hpp"

namespace {

vehicle_core::RawCanFrame frame(const std::uint64_t timestamp, const std::uint8_t bus,
                                const std::uint32_t identifier, const bool extended,
                                const bool remote, const std::uint8_t dlc) {
  vehicle_core::RawCanFrame value{};
  value.timestamp_us = timestamp;
  value.bus_id = bus;
  value.identifier = identifier;
  value.identifier_format = extended ? vehicle_core::CanIdentifierFormat::Extended
                                     : vehicle_core::CanIdentifierFormat::Standard;
  value.remote_request = remote;
  value.dlc = dlc;
  value.data = {0x01, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
  return value;
}

class FakeSource final : public raw_capture::FrameSource {
public:
  std::vector<vehicle_core::RawCanFrame> frames;
  std::size_t next{0};

  bool try_receive(vehicle_core::RawCanFrame &value) noexcept override {
    if (next == frames.size()) {
      return false;
    }
    value = frames[next++];
    return true;
  }
};

class FakeSink final : public raw_capture::OutputSink {
public:
  bool is_connected{true};
  bool would_block{false};
  std::vector<std::string> lines;

  bool connected() const noexcept override { return is_connected; }

  raw_capture::WriteResult write(const std::string_view line) noexcept override {
    if (!is_connected) {
      return raw_capture::WriteResult::kDisconnected;
    }
    if (would_block) {
      return raw_capture::WriteResult::kWouldBlock;
    }
    lines.emplace_back(line);
    return raw_capture::WriteResult::kWritten;
  }
};

} // namespace

TEST_CASE("exporter emits the normative v1 serialization exactly") {
  raw_capture::Configuration configuration{};
  configuration.session = {"mcan-tcan485+0.1.0", "tcan485-revA", 500'000, 1'000'000, 0, 0};
  configuration.diagnostic_interval_us = 0;
  configuration.statistics_interval_us = 0;
  raw_capture::Exporter exporter(configuration);

  FakeSource source;
  source.frames = {frame(1000, 0, 0x091, false, false, 8), frame(1100, 0, 0x202, false, false, 8),
                   frame(2100, 0, 0x1fffffff, true, false, 0),
                   frame(2200, 1, 0x123, false, true, 2)};
  source.frames[0].data = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  source.frames[1].data = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  FakeSink sink;
  REQUIRE(exporter.poll_input(source, 4) == 4);
  exporter.note_dropped_frames(2, 1200, 0, "queue-overflow");
  REQUIRE(exporter.poll_output(sink, 1000, 2) == 2);
  REQUIRE(exporter.poll_output(sink, 1100, 2) == 2);
  exporter.request_final_statistics();
  REQUIRE(exporter.poll_output(sink, 2200, 8) == 4);

  CHECK(sink.lines == std::vector<std::string>{
                          "MCAN-CAPTURE 1\n",
                          "SESSION firmware=mcan-tcan485%2B0.1.0 board=tcan485-revA "
                          "bitrate_bps=500000 clock=monotonic clock_unit=us "
                          "byte_order=big-endian clock_hz=1000000 dropped_frames=0 "
                          "dropped_records=0\n",
                          "FRAME t_us=1000 bus=0 id=0x091 format=std rtr=0 dlc=8 "
                          "data=0100000000000000\n",
                          "FRAME t_us=1100 bus=0 id=0x202 format=std rtr=0 dlc=8 "
                          "data=0000000000000000\n",
                          "DROP t_us=1200 bus=0 count=2 reason=queue-overflow\n",
                          "FRAME t_us=2100 bus=0 id=0x1fffffff format=ext rtr=0 dlc=0 data=-\n",
                          "FRAME t_us=2200 bus=1 id=0x123 format=std rtr=1 dlc=2 data=-\n",
                          "STATS t_us=2200 segment=0 dropped_frames=2 dropped_records=0\n"});
}

TEST_CASE("slow or disconnected output never blocks source draining") {
  raw_capture::Configuration configuration{};
  configuration.session = {"fw", "board", 500'000, 1'000'000, 0, 0};
  configuration.diagnostic_interval_us = 0;
  raw_capture::Exporter exporter(configuration);
  FakeSource source;
  for (std::uint32_t id = 0; id < 1000; ++id) {
    source.frames.push_back(frame(id, 0, id & 0x7ffU, false, false, 0));
  }
  FakeSink sink;
  sink.is_connected = false;
  CHECK(exporter.poll_input(source, source.frames.size()) == 1000);
  CHECK(exporter.statistics().input_frames == 1000);
  CHECK(exporter.statistics().queue_depth == raw_capture::kFrameQueueCapacity);
  CHECK(exporter.statistics().dropped_frames == 936);
  CHECK(exporter.statistics().queue_overflows == 936);
  CHECK(exporter.poll_output(sink, 1000, 100) == 0);
  sink.is_connected = true;
  CHECK(exporter.poll_output(sink, 1000, 3) == 3);
  CHECK(sink.lines[0] == "MCAN-CAPTURE 1\n");
  CHECK(sink.lines[1].rfind("SESSION ", 0) == 0);
  CHECK(exporter.statistics().dropped_frames == 936);
}

TEST_CASE("disconnect and reconnect create an isolated output segment") {
  raw_capture::Configuration configuration{};
  configuration.session = {"fw", "board", 500'000, 1'000'000, 0, 0};
  configuration.diagnostic_interval_us = 0;
  raw_capture::Exporter exporter(configuration);
  FakeSource source;
  source.frames = {frame(10, 0, 1, false, false, 0), frame(30, 0, 2, false, false, 0)};
  FakeSink sink;
  REQUIRE(exporter.poll_input(source, 1) == 1);
  REQUIRE(exporter.poll_output(sink, 10, 3) == 3);
  sink.is_connected = false;
  CHECK(exporter.poll_output(sink, 20, 3) == 0);
  REQUIRE(exporter.poll_input(source, 1) == 1);
  sink.is_connected = true;
  REQUIRE(exporter.poll_output(sink, 30, 3) == 2);
  CHECK(sink.lines[3] == "DISCONTINUITY t_us=30 bus=all segment=1 reason=usb-reconnect\n");
  CHECK(sink.lines[4].rfind("FRAME t_us=30 ", 0) == 0);
}

TEST_CASE("invalid configuration and frames fail closed with explicit loss") {
  raw_capture::Configuration invalid{};
  invalid.session = {"fw", "board", 0, 1'000'000, 0, 0};
  raw_capture::Exporter rejected(invalid);
  FakeSink sink;
  CHECK(rejected.failed());
  CHECK(rejected.poll_output(sink, 0, 4) == 0);

  raw_capture::Configuration configuration{};
  configuration.session = {"fw", "board", 500'000, 1'000'000, 0, 0};
  raw_capture::Exporter exporter(configuration);
  FakeSource source;
  source.frames.push_back(frame(10, 0, 0x123, false, false, 9));
  CHECK(exporter.poll_input(source, 1) == 1);
  CHECK(exporter.statistics().dropped_frames == 1);
  CHECK(exporter.poll_output(sink, 10, 4) == 3);
  CHECK(sink.lines[2] == "DROP t_us=10 bus=0 count=1 reason=invalid-frame\n");
}

TEST_CASE("separate loss boundaries cannot reorder a later frame") {
  raw_capture::Configuration configuration{};
  configuration.session = {"fw", "board", 500'000, 1'000'000, 0, 0};
  raw_capture::Exporter exporter(configuration);
  FakeSource source;
  for (std::uint64_t timestamp = 0; timestamp < raw_capture::kFrameQueueCapacity + 3; ++timestamp) {
    source.frames.push_back(
        frame(timestamp, 0, static_cast<std::uint32_t>(timestamp), false, false, 0));
  }
  CHECK(exporter.poll_input(source, source.frames.size()) == raw_capture::kFrameQueueCapacity + 3);
  FakeSink sink;
  CHECK(exporter.poll_output(sink, 100, 2) == 2);
  CHECK(exporter.poll_output(sink, 101, 1) == 1);
  CHECK(sink.lines.back().rfind("FRAME t_us=0 ", 0) == 0);
  CHECK(exporter.poll_output(sink, 102, 100) > 0);
  bool saw_drop = false;
  for (const auto &line : sink.lines) {
    if (line.rfind("DROP t_us=64 ", 0) == 0) {
      saw_drop = true;
    }
  }
  CHECK(saw_drop);
}
