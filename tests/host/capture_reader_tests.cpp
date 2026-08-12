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
    "SESSION firmware=mcan-tcan485%2B0.1.0 board=tcan485-revA bitrate_bps=500000 clock=monotonic "
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
} // namespace

TEST_CASE("golden capture parses all record types without loss") {
  const auto result = raw_capture::CaptureReader{}.read(golden_fixture());
  REQUIRE(result.ok);
  REQUIRE(result.errors.empty());
  CHECK(result.session.firmware == "mcan-tcan485+0.1.0");
  REQUIRE(result.records.size() == 8);
  CHECK(result.records[0].frame.identifier == 0x091);
  CHECK(result.records[0].frame.data[0] == 0x01);
  CHECK(result.records[5].frame.identifier == 0x1fffffff);
  CHECK(result.records[6].frame.remote_request);
  CHECK(result.records[6].frame.dlc == 2);
  CHECK(result.records[6].frame.data[0] == 0);
  CHECK(result.records[4].discontinuity.segment == 1);
}

TEST_CASE("writer round trip retains standard extended zero length and remote frames") {
  raw_capture::SessionMetadata session{"fw+synthetic", "board", 500000, 1000000, 0, 0};
  vehicle_core::RawCanFrame standard{};
  standard.timestamp_us = 10;
  standard.identifier = 0x123;
  standard.dlc = 2;
  standard.data[0] = 0xaa;
  standard.data[1] = 0x55;
  vehicle_core::RawCanFrame extended{};
  extended.timestamp_us = 11;
  extended.identifier = 0x1fffffff;
  extended.identifier_format = vehicle_core::CanIdentifierFormat::Extended;
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
  CHECK(result.records[0].frame.identifier == standard.identifier);
  CHECK(result.records[0].frame.data == standard.data);
  CHECK(result.records[1].frame.identifier_format == vehicle_core::CanIdentifierFormat::Extended);
  CHECK(result.records[2].frame.remote_request);
  CHECK(text.find("firmware=fw%2Bsynthetic") != std::string::npos);
}

TEST_CASE("strict parser reports malformed values and recovery preserves later frames") {
  const std::string fixture = golden_fixture();
  const std::string text = fixture.replace(fixture.find("dlc=8 data=0000000000000000"), 27,
                                           "dlc=9 data=0000000000000000");
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
  }) == 0);
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
}

TEST_CASE("recovery is transactional and validates unknown syntax") {
  const std::string text =
      "MCAN-CAPTURE 1\nSESSION firmware=f board=b bitrate_bps=1 clock=monotonic clock_unit=us "
      "byte_order=big-endian clock_hz=1 dropped_frames=0 dropped_records=0\n"
      "DROP t_us=1 bus=0 count=0 reason=bad\n"
      "DISCONTINUITY t_us=2 bus=all segment=0 reason=bad\n"
      "FUTURE thing=value\n"
      "FRAME t_us=3 bus=0 id=0x001 format=std rtr=0 dlc=0 data=-\n";
  const auto result = raw_capture::CaptureReader{}.read(text, raw_capture::ParseMode::Recovery);
  CHECK(result.ok);
  CHECK(result.errors.size() == 2);
  REQUIRE(result.records.size() == 1);
  CHECK(result.records[0].frame.timestamp_us == 3);
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
  CHECK(replay.advance_to(1101, [&](const vehicle_core::RawCanFrame &,
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
