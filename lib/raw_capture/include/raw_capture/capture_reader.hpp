#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "vehicle_core/vehicle_core.hpp"

namespace raw_capture {

struct SessionMetadata {
  std::string firmware;
  std::string board;
  std::uint32_t bitrate_bps{0};
  std::uint64_t clock_hz{0};
  std::uint64_t dropped_frames{0};
  std::uint64_t dropped_records{0};
};

struct DropRecord {
  std::uint64_t timestamp_us{0};
  bool all_buses{false};
  std::uint8_t bus_id{0};
  std::uint64_t count{0};
  std::string reason;
};

struct StatisticsRecord {
  std::uint64_t timestamp_us{0};
  std::uint64_t segment{0};
  std::uint64_t dropped_frames{0};
  std::uint64_t dropped_records{0};
};

struct DiscontinuityRecord {
  std::uint64_t timestamp_us{0};
  bool all_buses{false};
  std::uint8_t bus_id{0};
  std::uint64_t segment{0};
  std::string reason;
};

enum class RecordType : std::uint8_t { Frame, Drop, Statistics, Discontinuity };

struct CaptureRecord {
  RecordType type{RecordType::Frame};
  std::uint64_t segment{0};
  vehicle_core::RawCanFrame frame{};
  DropRecord drop{};
  StatisticsRecord statistics{};
  DiscontinuityRecord discontinuity{};

  static CaptureRecord frame_record(const vehicle_core::RawCanFrame &value,
                                    std::uint64_t segment = 0) noexcept;
  static CaptureRecord drop_record(const DropRecord &value, std::uint64_t segment = 0);
  static CaptureRecord statistics_record(const StatisticsRecord &value);
  static CaptureRecord discontinuity_record(const DiscontinuityRecord &value);
};

struct ParseError {
  std::size_t line{0};
  std::string message;
  std::string context;
};

enum class ParseMode : std::uint8_t { Strict, Recovery };

struct ParseResult {
  bool ok{false};
  SessionMetadata session{};
  std::vector<CaptureRecord> records;
  std::vector<ParseError> errors;
};

class CaptureReader final {
public:
  [[nodiscard]] ParseResult read(std::string_view text, ParseMode mode = ParseMode::Strict) const;
  [[nodiscard]] ParseResult parse(std::string_view text, ParseMode mode = ParseMode::Strict) const {
    return read(text, mode);
  }
};

using CaptureParser = CaptureReader;

} // namespace raw_capture
