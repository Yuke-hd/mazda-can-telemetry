#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "raw_capture/capture_reader.hpp"
#include "raw_capture/capture_writer.hpp"
#include "raw_capture/replay.hpp"
#include "vehicle_core/vehicle_core.hpp"

namespace {
const char *const kGoldenFallback =
    "MCAN-CAPTURE 1\n"
    "SESSION firmware=mcan-weact-can485-v11%2B0.1.0 board=weact-can485-v1.1 "
    "bitrate_bps=500000 clock=monotonic "
    "clock_unit=us byte_order=big-endian clock_hz=1000000 dropped_frames=0 dropped_records=0\n"
    "FRAME t_us=1000 bus=0 id=0x091 format=std rtr=0 dlc=8 data=0100000000000000\n"
    "FRAME t_us=1100 bus=0 id=0x202 format=std rtr=0 dlc=8 data=0000000000000000\n"
    "DROP t_us=1200 bus=0 count=2 reason=queue-overflow\n"
    "STATS t_us=1200 segment=0 dropped_frames=2 dropped_records=0\n"
    "DISCONTINUITY t_us=2000 bus=all segment=1 reason=clock-reset\n"
    "FRAME t_us=2100 bus=0 id=0x1fffffff format=ext rtr=0 dlc=0 data=-\n"
    "FRAME t_us=2200 bus=1 id=0x123 format=std rtr=1 dlc=2 data=-\n"
    "STATS t_us=2200 segment=1 dropped_frames=2 dropped_records=0\n";

std::string golden_fixture() {
#ifdef CAPTURE_FIXTURE_PATH
  std::ifstream file(CAPTURE_FIXTURE_PATH, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
#else
  return kGoldenFallback;
#endif
}

void check_frame(const vehicle_core::RawCanFrame &actual,
                 const vehicle_core::RawCanFrame &expected) {
  CHECK(actual.timestamp_us == expected.timestamp_us);
  CHECK(actual.bus_id == expected.bus_id);
  CHECK(actual.identifier == expected.identifier);
  CHECK(actual.identifier_format == expected.identifier_format);
  CHECK(actual.remote_request == expected.remote_request);
  CHECK(actual.dlc == expected.dlc);
  CHECK(actual.data == expected.data);
}
} // namespace

TEST_CASE("golden capture parses all record types without loss") {
  const auto result = raw_capture::CaptureReader{}.read(golden_fixture());
  REQUIRE(result.ok);
  REQUIRE(result.errors.empty());
  CHECK(result.session.firmware == "mcan-weact-can485-v11+0.1.0");
  REQUIRE(result.records.size() == 8);
  vehicle_core::RawCanFrame expected0{};
  expected0.timestamp_us = 1000;
  expected0.identifier = 0x091;
  expected0.dlc = 8;
  expected0.data[0] = 0x01;
  vehicle_core::RawCanFrame expected1{};
  expected1.timestamp_us = 1100;
  expected1.identifier = 0x202;
  expected1.dlc = 8;
  vehicle_core::RawCanFrame expected5{};
  expected5.timestamp_us = 2100;
  expected5.identifier = 0x1fffffff;
  expected5.identifier_format = vehicle_core::CanIdentifierFormat::Extended;
  vehicle_core::RawCanFrame expected6{};
  expected6.timestamp_us = 2200;
  expected6.bus_id = 1;
  expected6.identifier = 0x123;
  expected6.remote_request = true;
  expected6.dlc = 2;
  check_frame(result.records[0].frame, expected0);
  check_frame(result.records[1].frame, expected1);
  check_frame(result.records[5].frame, expected5);
  check_frame(result.records[6].frame, expected6);
  CHECK(result.records[4].discontinuity.segment == 1);
}

TEST_CASE("writer round trip retains standard extended zero length and remote frames") {
  raw_capture::SessionMetadata session{"fw+synthetic", "board", 500000, 1000000, 0, 0};
  vehicle_core::RawCanFrame standard{};
  standard.timestamp_us = 10;
  standard.bus_id = 2;
  standard.identifier = 0x123;
  standard.dlc = 2;
  standard.data[0] = 0xaa;
  standard.data[1] = 0x55;
  vehicle_core::RawCanFrame extended{};
  extended.timestamp_us = 11;
  extended.bus_id = 1;
  extended.identifier = 0x1fffffff;
  extended.identifier_format = vehicle_core::CanIdentifierFormat::Extended;
  extended.dlc = 3;
  extended.data[0] = 0x12;
  extended.data[1] = 0xab;
  extended.data[2] = 0xef;
  vehicle_core::RawCanFrame remote{};
  remote.timestamp_us = 12;
  remote.identifier = 0x321;
  remote.remote_request = true;
  remote.dlc = 8;
  const std::vector<raw_capture::CaptureRecord> records{
      raw_capture::CaptureRecord::frame_record(standard),
      raw_capture::CaptureRecord::frame_record(extended),
      raw_capture::CaptureRecord::frame_record(remote)};
  std::string text;
  REQUIRE(raw_capture::CaptureWriter::write(session, records, text));
  const auto result = raw_capture::CaptureReader{}.read(text);
  REQUIRE(result.ok);
  REQUIRE(result.records.size() == records.size());
  check_frame(result.records[0].frame, standard);
  check_frame(result.records[1].frame, extended);
  check_frame(result.records[2].frame, remote);
  CHECK(text.find("firmware=fw%2Bsynthetic") != std::string::npos);
}

TEST_CASE("strict parser reports malformed values and recovery preserves later frames") {
  std::string text = golden_fixture();
  text.replace(text.find("dlc=8 data=0000000000000000"), 27, "dlc=9 data=0000000000000000");
  const auto strict = raw_capture::CaptureReader{}.read(text, raw_capture::ParseMode::Strict);
  CHECK_FALSE(strict.ok);
  REQUIRE(strict.errors.size() == 1);
  CHECK(strict.errors[0].line == 4);
  const auto recovery = raw_capture::CaptureReader{}.read(text, raw_capture::ParseMode::Recovery);
  CHECK(recovery.ok);
  CHECK(recovery.errors.size() == 1);
  CHECK(recovery.records.size() == 7);
  CHECK(recovery.records[0].frame.timestamp_us == 1000);
  CHECK(recovery.records[1].type == raw_capture::RecordType::Drop);
}

TEST_CASE("replay clock can pause and advance deterministically") {
  const auto parsed = raw_capture::CaptureReader{}.read(golden_fixture());
  REQUIRE(parsed.ok);
  raw_capture::SimulatedMonotonicClock clock;
  raw_capture::ReplayHarness replay{clock};
  std::vector<std::uint64_t> times;
  replay.load(parsed.records);
  replay.pause();
  CHECK(replay.advance_to(1000, [&](const vehicle_core::RawCanFrame &,
                                    raw_capture::SimulatedMonotonicClock &now) {
    times.push_back(now.now());
  }) == 0);
  CHECK(times.empty());
  replay.resume();
  CHECK(replay.advance_to(1050, [&](const vehicle_core::RawCanFrame &,
                                    raw_capture::SimulatedMonotonicClock &now) {
    times.push_back(now.now());
  }) == 1);
  CHECK(replay.advance_to(2200, [&](const vehicle_core::RawCanFrame &,
                                    raw_capture::SimulatedMonotonicClock &now) {
    times.push_back(now.now());
  }) == 1);
  CHECK(replay.advance_to(2200, [&](const vehicle_core::RawCanFrame &,
                                    raw_capture::SimulatedMonotonicClock &now) {
    times.push_back(now.now());
  }) == 2);
  CHECK(times == std::vector<std::uint64_t>{1000, 1100, 2100, 2200});
  clock.pause();
  clock.advance(500);
  CHECK(clock.now() == 2200);
  clock.resume();
  clock.advance(500);
  CHECK(clock.now() == 2700);

  raw_capture::SimulatedMonotonicClock drain_clock;
  raw_capture::ReplayHarness drain_replay{drain_clock};
  std::size_t drained = 0;
  drain_replay.replay(parsed.records, [&](const vehicle_core::RawCanFrame &,
                                          raw_capture::SimulatedMonotonicClock &) { ++drained; });
  CHECK(drained == 4);

  raw_capture::SimulatedMonotonicClock reset_clock;
  raw_capture::ReplayHarness reset_replay{reset_clock};
  const auto reset_capture = raw_capture::CaptureReader{}.read(
      "MCAN-CAPTURE 1\nSESSION firmware=f board=b bitrate_bps=1 clock=monotonic clock_unit=us "
      "byte_order=big-endian clock_hz=1 dropped_frames=0 dropped_records=0\n"
      "DISCONTINUITY t_us=2000 bus=all segment=1 reason=clock-reset\n"
      "FRAME t_us=10 bus=0 id=0x001 format=std rtr=0 dlc=0 data=-\n");
  REQUIRE(reset_capture.ok);
  reset_replay.load(reset_capture.records);
  std::uint64_t observed = 0;
  CHECK(reset_replay.advance_to(
            2000, [&](const vehicle_core::RawCanFrame &,
                      raw_capture::SimulatedMonotonicClock &now) { observed = now.now(); }) == 0);
  REQUIRE(reset_replay.advance_to(
              10, [&](const vehicle_core::RawCanFrame &,
                      raw_capture::SimulatedMonotonicClock &now) { observed = now.now(); }) == 1);
  CHECK(observed == 10);
}

TEST_CASE("version truncation payload and timestamp failures are explicit") {
  const std::string prefix =
      "MCAN-CAPTURE 1\nSESSION firmware=f board=b bitrate_bps=1 clock=monotonic clock_unit=us "
      "byte_order=big-endian clock_hz=1 dropped_frames=0 dropped_records=0\n";
  for (const std::string line : {"FRAME t_us=1 bus=0 id=0x001 format=std rtr=0 dlc=2 data=00",
                                 "FRAME t_us=0 bus=0 id=0x001 format=std rtr=0 dlc=1 data=zz"}) {
    const auto parsed = raw_capture::CaptureReader{}.read(prefix + line + "\n");
    CHECK_FALSE(parsed.ok);
    CHECK(parsed.errors.size() == 1);
  }
  const auto regression = raw_capture::CaptureReader{}.read(
      prefix + "FRAME t_us=2 bus=0 id=0x001 format=std rtr=0 dlc=0 data=-\n"
               "FRAME t_us=1 bus=0 id=0x001 format=std rtr=0 dlc=0 data=-\n");
  CHECK_FALSE(regression.ok);
  REQUIRE(regression.errors.size() == 1);
  const auto unsupported = raw_capture::CaptureReader{}.read("MCAN-CAPTURE 2\n");
  CHECK_FALSE(unsupported.ok);
  const auto truncated = raw_capture::CaptureReader{}.read(
      prefix + "FRAME t_us=1 bus=0 id=0x001 format=std rtr=0 dlc=0 data=-");
  CHECK_FALSE(truncated.ok);
  REQUIRE(truncated.errors.size() == 1);
  CHECK(truncated.errors[0].message == "truncated record without LF");
  for (const std::string line :
       {"FRAME t_us=01 bus=0 id=0x001 format=std rtr=0 dlc=0 data=-\n",
        "FRAME t_us=1 bus=0 id=0X001 format=std rtr=0 dlc=0 data=-\n",
        "FRAME t_us=1 bus=0 id=0x001 format=std rtr=0 dlc=0 data=- t_us=2\n"}) {
    const auto malformed = raw_capture::CaptureReader{}.read(prefix + line);
    CHECK_FALSE(malformed.ok);
    CHECK(malformed.errors.size() == 1);
  }
  const auto uppercase_digit = raw_capture::CaptureReader{}.read(
      prefix + "FRAME t_us=1 bus=0 id=0x00A format=std rtr=0 dlc=0 data=-\n");
  CHECK_FALSE(uppercase_digit.ok);
}

TEST_CASE("recovery is transactional and validates unknown syntax") {
  const std::string text =
      "MCAN-CAPTURE 1\nSESSION firmware=f board=b bitrate_bps=1 clock=monotonic clock_unit=us "
      "byte_order=big-endian clock_hz=1 dropped_frames=0 dropped_records=0\n"
      "DROP t_us=100 bus=0 count=0 reason=bad\n"
      "DISCONTINUITY t_us=200 bus=999 segment=1 reason=bad\n"
      "STATS t_us=300 segment=0 dropped_frames=9 dropped_records=1\n"
      "FUTURE thing=value\n"
      "FRAME t_us=50 bus=2 id=0x001 format=std rtr=0 dlc=0 data=-\n"
      "DROP t_us=60 bus=2 count=2 reason=synthetic\n"
      "STATS t_us=60 segment=0 dropped_frames=2 dropped_records=1\n"
      "STATS t_us=70 segment=0 dropped_frames=2 dropped_records=0\n"
      "FRAME t_us=65 bus=2 id=0x001 format=std rtr=0 dlc=0 data=-\n"
      "STATS t_us=65 segment=0 dropped_frames=2 dropped_records=1\n";
  const auto result = raw_capture::CaptureReader{}.read(text, raw_capture::ParseMode::Recovery);
  CHECK(result.ok);
  REQUIRE(result.errors.size() == 4);
  CHECK(result.errors[0].line == 3);
  CHECK(result.errors[1].line == 4);
  CHECK(result.errors[2].line == 5);
  CHECK(result.errors[3].line == 10);
  REQUIRE(result.records.size() == 5);
  CHECK(result.records[0].frame.timestamp_us == 50);
  CHECK(result.records[1].drop.count == 2);
  CHECK(result.records[2].statistics.dropped_frames == 2);
  CHECK(result.records[3].frame.timestamp_us == 65);
  CHECK(result.records[4].statistics.dropped_records == 1);
  const auto duplicate = raw_capture::CaptureReader{}.read(
      "MCAN-CAPTURE 1\nSESSION firmware=f board=b bitrate_bps=1 clock=monotonic clock_unit=us "
      "byte_order=big-endian clock_hz=1 dropped_frames=0 dropped_records=0\n"
      "SESSION firmware=f board=b bitrate_bps=1 clock=monotonic clock_unit=us "
      "byte_order=big-endian clock_hz=1 dropped_frames=0 dropped_records=0\n");
  CHECK_FALSE(duplicate.ok);
}

TEST_CASE("percent encoding is canonical and decoded text is UTF-8") {
  const std::string prefix = "MCAN-CAPTURE 1\nSESSION firmware=";
  const std::string suffix =
      " board=b bitrate_bps=1 clock=monotonic clock_unit=us byte_order=big-endian clock_hz=1 "
      "dropped_frames=0 dropped_records=0\n";
  CHECK_FALSE(raw_capture::CaptureReader{}.read(prefix + "%41" + suffix).ok);
  CHECK_FALSE(raw_capture::CaptureReader{}.read(prefix + "%FF" + suffix).ok);
  CHECK(raw_capture::CaptureReader{}.read(prefix + "f%2B1" + suffix).ok);
  raw_capture::SessionMetadata session{"firmware + café", "board rev", 1, 1, 0, 0};
  std::string encoded;
  REQUIRE(raw_capture::CaptureWriter::write(session, {}, encoded));
  CHECK(encoded.find("firmware%20%2B%20caf%C3%A9") != std::string::npos);
  const auto round_trip = raw_capture::CaptureReader{}.read(encoded);
  REQUIRE(round_trip.ok);
  CHECK(round_trip.session.firmware == session.firmware);
}

TEST_CASE("discontinuity starts a fresh timestamp ordering segment") {
  const auto result = raw_capture::CaptureReader{}.read(
      "MCAN-CAPTURE 1\nSESSION firmware=f board=b bitrate_bps=1 clock=monotonic clock_unit=us "
      "byte_order=big-endian clock_hz=1 dropped_frames=0 dropped_records=0\n"
      "FRAME t_us=2000 bus=0 id=0x001 format=std rtr=0 dlc=0 data=-\n"
      "DISCONTINUITY t_us=3000 bus=all segment=1 reason=clock-reset\n"
      "FRAME t_us=10 bus=0 id=0x001 format=std rtr=0 dlc=0 data=-\n");
  REQUIRE(result.ok);
  REQUIRE(result.records.size() == 3);
  CHECK(result.records[2].frame.timestamp_us == 10);
}

TEST_CASE("unknown fields and records require valid syntax") {
  const std::string prefix =
      "MCAN-CAPTURE 1\nSESSION firmware=f board=b bitrate_bps=1 clock=monotonic clock_unit=us "
      "byte_order=big-endian clock_hz=1 dropped_frames=0 dropped_records=0\n";
  const auto known = raw_capture::CaptureReader{}.read(
      prefix + "FRAME t_us=1 bus=0 id=0x001 format=std rtr=0 dlc=0 data=- future=value\n");
  CHECK(known.ok);
  REQUIRE(known.records.size() == 1);
  const auto malformed = raw_capture::CaptureReader{}.read(prefix + "FUTURE future=%zz\n",
                                                           raw_capture::ParseMode::Recovery);
  CHECK(malformed.ok);
  REQUIRE(malformed.errors.size() == 1);
}

TEST_CASE("replay clock drives vehicle freshness timeout") {
  raw_capture::SimulatedMonotonicClock clock;
  raw_capture::ReplayHarness replay{clock};
  vehicle_core::VehicleFreshnessPolicy policy{};
  policy.turn_state_timeout_us = 100;
  vehicle_core::VehicleStateStore store{clock, policy};
  vehicle_core::RawCanFrame frame{};
  frame.timestamp_us = 1000;
  frame.identifier = 1;
  replay.load({raw_capture::CaptureRecord::frame_record(frame)});
  REQUIRE(replay.advance_to(1000, [&](const vehicle_core::RawCanFrame &value,
                                      raw_capture::SimulatedMonotonicClock &) {
    REQUIRE(store.mutable_state()
                .update_turn(vehicle_core::TurnState::Left, value.timestamp_us)
                .has_value());
  }) == 1);
  CHECK(store.snapshot().turn_state.is_valid());
  CHECK(replay.advance_by(100, [&](const vehicle_core::RawCanFrame &,
                                   raw_capture::SimulatedMonotonicClock &) {}) == 0);
  CHECK(store.snapshot().turn_state.is_valid());
  CHECK(replay.advance_by(1, [&](const vehicle_core::RawCanFrame &,
                                 raw_capture::SimulatedMonotonicClock &) {}) == 0);
  CHECK(store.snapshot().turn_state.is_stale());
}

TEST_CASE("timestamp ordering and counters are validated") {
  const std::string bad =
      "MCAN-CAPTURE 1\nSESSION firmware=f board=b bitrate_bps=1 clock=monotonic clock_unit=us "
      "byte_order=big-endian clock_hz=1 dropped_frames=1 dropped_records=0\n"
      "DROP t_us=2 bus=0 count=1 reason=x\nSTATS t_us=3 segment=0 dropped_frames=1 "
      "dropped_records=0\n";
  const auto result = raw_capture::CaptureReader{}.read(bad);
  CHECK_FALSE(result.ok);
  REQUIRE(result.errors.size() == 1);
  CHECK(result.errors[0].line == 4);
}
