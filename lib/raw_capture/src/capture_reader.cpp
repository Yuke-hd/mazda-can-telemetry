#include "raw_capture/capture_reader.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <map>
#include <utility>

namespace raw_capture {
namespace {

using Fields = std::map<std::string_view, std::string_view>;

bool utf8(const std::string_view value) {
  for (std::size_t i = 0; i < value.size();) {
    const auto lead = static_cast<unsigned char>(value[i]);
    if (lead <= 0x7fU) {
      ++i;
      continue;
    }
    const std::size_t width = lead >= 0xf0U && lead <= 0xf4U   ? 4
                              : lead >= 0xe0U && lead <= 0xefU ? 3
                              : lead >= 0xc2U && lead <= 0xdfU ? 2
                                                               : 0;
    if (width == 0 || i + width > value.size())
      return false;
    for (std::size_t j = 1; j < width; ++j) {
      if ((static_cast<unsigned char>(value[i + j]) & 0xc0U) != 0x80U)
        return false;
    }
    const auto second = static_cast<unsigned char>(value[i + 1]);
    if ((lead == 0xe0U && second < 0xa0U) || (lead == 0xedU && second >= 0xa0U) ||
        (lead == 0xf0U && second < 0x90U) || (lead == 0xf4U && second >= 0x90U))
      return false;
    i += width;
  }
  return true;
}

bool hex_digit(const char c, unsigned &value) {
  if (c >= '0' && c <= '9')
    value = static_cast<unsigned>(c - '0');
  else if (c >= 'A' && c <= 'F')
    value = static_cast<unsigned>(c - 'A' + 10);
  else if (c >= 'a' && c <= 'f')
    value = static_cast<unsigned>(c - 'a' + 10);
  else
    return false;
  return true;
}

bool encoded_syntax(const std::string_view value) {
  if (!utf8(value))
    return false;
  for (std::size_t i = 0; i < value.size(); ++i) {
    const auto c = static_cast<unsigned char>(value[i]);
    if (c > 0x7fU)
      return false;
    if (c == '%') {
      unsigned ignored = 0;
      if (i + 2 >= value.size() || !hex_digit(value[i + 1], ignored) ||
          !hex_digit(value[i + 2], ignored) ||
          !(value[i + 1] >= '0' && value[i + 1] <= '9' ||
            value[i + 1] >= 'A' && value[i + 1] <= 'F')) {
        return false;
      }
      if (!(value[i + 2] >= '0' && value[i + 2] <= '9' ||
            value[i + 2] >= 'A' && value[i + 2] <= 'F'))
        return false;
      i += 2;
    }
  }
  return true;
}

bool unreserved(const unsigned char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
         c == '.' || c == '_' || c == '~';
}

bool decode_percent(const std::string_view encoded, std::string &decoded) {
  if (!encoded_syntax(encoded))
    return false;
  decoded.clear();
  for (std::size_t i = 0; i < encoded.size(); ++i) {
    const auto c = static_cast<unsigned char>(encoded[i]);
    if (c != '%') {
      if (!unreserved(c))
        return false;
      decoded.push_back(static_cast<char>(c));
      continue;
    }
    unsigned high = 0, low = 0;
    (void)hex_digit(encoded[i + 1], high);
    (void)hex_digit(encoded[i + 2], low);
    const auto byte = static_cast<unsigned char>((high << 4U) | low);
    if (unreserved(byte))
      return false; // canonical form uses the literal byte.
    decoded.push_back(static_cast<char>(byte));
    i += 2;
  }
  return utf8(decoded);
}

bool decimal(const std::string_view text, std::uint64_t &value) {
  if (text.empty() || (text.size() > 1 && text.front() == '0'))
    return false;
  for (const char c : text)
    if (c < '0' || c > '9')
      return false;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
  return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool identifier(const std::string_view text, const std::size_t digits, std::uint32_t &value) {
  if (text.size() != digits + 2 || text.substr(0, 2) != "0x")
    return false;
  value = 0;
  for (std::size_t i = 2; i < text.size(); ++i) {
    unsigned digit = 0;
    if (!((text[i] >= '0' && text[i] <= '9') || (text[i] >= 'a' && text[i] <= 'f')) ||
        !hex_digit(text[i], digit))
      return false;
    value = (value << 4U) | digit;
  }
  return true;
}

bool fields(const std::string_view line, std::string_view &type, Fields &out, std::string &error) {
  const auto first_space = line.find(' ');
  type = first_space == std::string_view::npos ? line : line.substr(0, first_space);
  if (type.empty()) {
    error = "missing record type";
    return false;
  }
  for (const char c : type)
    if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) {
      error = "invalid record type";
      return false;
    }
  std::size_t start = first_space == std::string_view::npos ? line.size() : first_space + 1;
  while (start < line.size()) {
    const auto end = line.find(' ', start);
    const auto token =
        line.substr(start, end == std::string_view::npos ? line.size() - start : end - start);
    const auto equal = token.find('=');
    if (equal == std::string_view::npos || equal == 0 || equal + 1 == token.size()) {
      error = "field must be key=value";
      return false;
    }
    const auto key = token.substr(0, equal);
    for (const char c : key)
      if (!(c >= 'a' && c <= 'z' || c >= '0' && c <= '9' || c == '_')) {
        error = "invalid field name";
        return false;
      }
    const auto value = token.substr(equal + 1);
    if (!encoded_syntax(value)) {
      error = "invalid field value encoding";
      return false;
    }
    if (!out.emplace(key, value).second) {
      error = "duplicate field";
      return false;
    }
    if (end == std::string_view::npos)
      break;
    start = end + 1;
  }
  return true;
}

bool required(const Fields &map, const char *const *names, const std::size_t count,
              std::string &error) {
  for (std::size_t i = 0; i < count; ++i)
    if (map.find(names[i]) == map.end()) {
      error = std::string("missing field ") + names[i];
      return false;
    }
  return true;
}

bool bus(const std::string_view text, bool &all, std::uint8_t &value) {
  if (text == "all") {
    all = true;
    value = 0;
    return true;
  }
  std::uint64_t parsed = 0;
  if (!decimal(text, parsed) || parsed > 255)
    return false;
  all = false;
  value = static_cast<std::uint8_t>(parsed);
  return true;
}

template <typename T> bool get_number(const Fields &map, const char *name, T &value) {
  std::uint64_t parsed = 0;
  const auto it = map.find(name);
  if (it == map.end() || !decimal(it->second, parsed) || parsed > std::numeric_limits<T>::max())
    return false;
  value = static_cast<T>(parsed);
  return true;
}

bool timestamp_ok(const std::uint64_t timestamp, const std::uint64_t last, const bool have_last) {
  return !have_last || timestamp >= last;
}

} // namespace

CaptureRecord CaptureRecord::frame_record(const vehicle_core::RawCanFrame &value,
                                          const std::uint64_t segment_value) noexcept {
  CaptureRecord result;
  result.type = RecordType::Frame;
  result.segment = segment_value;
  result.frame = value;
  return result;
}
CaptureRecord CaptureRecord::drop_record(const DropRecord &value,
                                         const std::uint64_t segment_value) {
  CaptureRecord result;
  result.type = RecordType::Drop;
  result.segment = segment_value;
  result.drop = value;
  return result;
}
CaptureRecord CaptureRecord::statistics_record(const StatisticsRecord &value) {
  CaptureRecord result;
  result.type = RecordType::Statistics;
  result.segment = value.segment;
  result.statistics = value;
  return result;
}
CaptureRecord CaptureRecord::discontinuity_record(const DiscontinuityRecord &value) {
  CaptureRecord result;
  result.type = RecordType::Discontinuity;
  result.segment = value.segment;
  result.discontinuity = value;
  return result;
}

ParseResult CaptureReader::read(const std::string_view text, const ParseMode mode) const {
  ParseResult result;
  auto report = [&](const std::size_t line, const std::string &message,
                    const std::string_view context) {
    result.errors.push_back(ParseError{line, message, std::string(context)});
  };
  const auto line_end = text.find('\n');
  if (line_end == std::string_view::npos || text.substr(0, line_end) != "MCAN-CAPTURE 1") {
    report(1, "invalid or missing v1 header",
           line_end == std::string_view::npos ? text : text.substr(0, line_end));
    return result;
  }
  bool session_seen = false, have_timestamp = false;
  std::uint64_t segment = 0, last_timestamp = 0, expected_dropped_frames = 0,
                expected_dropped_records = 0;
  std::size_t line_number = 1;
  std::size_t start = line_end + 1;
  while (start <= text.size()) {
    const auto end = text.find('\n', start);
    const auto raw =
        text.substr(start, end == std::string_view::npos ? text.size() - start : end - start);
    ++line_number;
    if (end == std::string_view::npos && !raw.empty()) {
      report(line_number, "truncated record without LF", raw);
      if (mode == ParseMode::Strict) {
        result.ok = false;
        return result;
      }
      break;
    }
    std::string_view line = raw;
    if (!utf8(line)) {
      report(line_number, "invalid UTF-8", line);
      if (mode == ParseMode::Strict) {
        result.ok = false;
        return result;
      }
      if (end == std::string_view::npos)
        break;
      start = end + 1;
      continue;
    }
    if (!line.empty() && line.back() == '\r') {
      report(line_number, "CR line ending is not permitted", line);
      if (mode == ParseMode::Strict) {
        result.ok = false;
        return result;
      }
      if (end == std::string_view::npos)
        break;
      start = end + 1;
      continue;
    }
    if (line.empty() || line.front() == '#') {
      if (end == std::string_view::npos)
        break;
      start = end + 1;
      continue;
    }
    Fields map;
    std::string_view type;
    std::string error;
    if (!fields(line, type, map, error)) {
      report(line_number, error, line);
      if (mode == ParseMode::Strict) {
        result.ok = false;
        return result;
      }
    } else if (!session_seen) {
      static const char *names[] = {"firmware", "board",          "bitrate_bps",
                                    "clock",    "clock_unit",     "byte_order",
                                    "clock_hz", "dropped_frames", "dropped_records"};
      if (type != "SESSION" || !required(map, names, 9, error)) {
        report(line_number, type != "SESSION" ? "SESSION must immediately follow header" : error,
               line);
        if (mode == ParseMode::Strict) {
          result.ok = false;
          return result;
        }
      } else {
        std::string firmware, board;
        std::uint64_t bitrate = 0, clock_hz = 0, dropped_frames = 0, dropped_records = 0;
        if (!decode_percent(map["firmware"], firmware) || !decode_percent(map["board"], board) ||
            firmware.empty() || board.empty() || map["clock"] != "monotonic" ||
            map["clock_unit"] != "us" || map["byte_order"] != "big-endian" ||
            !decimal(map["bitrate_bps"], bitrate) || bitrate == 0 ||
            !decimal(map["clock_hz"], clock_hz) || clock_hz == 0 ||
            !decimal(map["dropped_frames"], dropped_frames) ||
            !decimal(map["dropped_records"], dropped_records) ||
            bitrate > std::numeric_limits<std::uint32_t>::max()) {
          report(line_number, "invalid SESSION value", line);
          if (mode == ParseMode::Strict) {
            result.ok = false;
            return result;
          }
        } else {
          result.session = SessionMetadata{
              std::move(firmware), std::move(board), static_cast<std::uint32_t>(bitrate), clock_hz,
              dropped_frames,      dropped_records};
          expected_dropped_frames = dropped_frames;
          expected_dropped_records = dropped_records;

          session_seen = true;
          result.ok = true;
        }
      }
    } else if (type == "SESSION") {
      report(line_number, "duplicate SESSION record", line);
      if (mode == ParseMode::Strict) {
        result.ok = false;
        return result;
      }
    } else if (type == "FRAME" || type == "DROP" || type == "STATS" || type == "DISCONTINUITY") {
      bool valid = true;
      bool starts_new_segment = false;
      CaptureRecord record;
      record.segment = segment;
      std::uint64_t timestamp = 0;
      if (type == "FRAME") {
        static const char *names[] = {"t_us", "bus", "id", "format", "rtr", "dlc", "data"};
        valid = required(map, names, 7, error) && get_number(map, "t_us", timestamp);
        std::uint64_t bus_id = 0, rtr = 0, dlc = 0;
        std::uint32_t id = 0;
        valid = valid && get_number(map, "bus", bus_id) && bus_id <= 255 &&
                (map["format"] == "std" || map["format"] == "ext") && get_number(map, "rtr", rtr) &&
                rtr <= 1 && get_number(map, "dlc", dlc) && dlc <= 8;
        const auto digits = map["format"] == "ext" ? 8U : 3U;
        valid = valid && identifier(map["id"], digits, id) &&
                ((map["format"] == "std" && id <= 0x7ffU) ||
                 (map["format"] == "ext" && id <= 0x1fffffffU));
        vehicle_core::RawCanFrame frame{};
        frame.timestamp_us = timestamp;
        frame.bus_id = static_cast<std::uint8_t>(bus_id);
        frame.identifier = id;
        frame.identifier_format = map["format"] == "ext"
                                      ? vehicle_core::CanIdentifierFormat::Extended
                                      : vehicle_core::CanIdentifierFormat::Standard;
        frame.remote_request = rtr != 0;
        frame.dlc = static_cast<std::uint8_t>(dlc);
        const auto data = map["data"];
        if (frame.remote_request || frame.dlc == 0)
          valid = valid && data == "-";
        else if (data.size() == frame.dlc * 2U)
          for (std::size_t i = 0; i < frame.dlc && valid; ++i) {
            unsigned hi = 0, lo = 0;
            valid = hex_digit(data[i * 2], hi) && hex_digit(data[i * 2 + 1], lo);
            frame.data[i] = static_cast<std::uint8_t>((hi << 4U) | lo);
          }
        else
          valid = false;
        valid =
            valid && frame.is_valid() && timestamp_ok(timestamp, last_timestamp, have_timestamp);
        if (valid)
          record = CaptureRecord::frame_record(frame, segment);
      } else if (type == "DROP") {
        static const char *names[] = {"t_us", "bus", "count", "reason"};
        valid = required(map, names, 4, error) && get_number(map, "t_us", timestamp);
        std::uint64_t count = 0;
        bool all = false;
        std::uint8_t bus_id = 0;
        std::string reason;
        valid = valid && bus(map["bus"], all, bus_id) && get_number(map, "count", count) &&
                count != 0 && decode_percent(map["reason"], reason);
        valid = valid && timestamp_ok(timestamp, last_timestamp, have_timestamp) &&
                count <= std::numeric_limits<std::uint64_t>::max() - expected_dropped_frames;
        if (valid) {
          expected_dropped_frames += count;
          record = CaptureRecord::drop_record(
              DropRecord{timestamp, all, bus_id, count, std::move(reason)}, segment);
        }
      } else if (type == "STATS") {
        static const char *names[] = {"t_us", "segment", "dropped_frames", "dropped_records"};
        valid = required(map, names, 4, error) && get_number(map, "t_us", timestamp);
        std::uint64_t stats_segment = 0, dropped = 0, records = 0;
        valid = valid && get_number(map, "segment", stats_segment) &&
                get_number(map, "dropped_frames", dropped) &&
                get_number(map, "dropped_records", records) && stats_segment == segment &&
                dropped == expected_dropped_frames && records >= result.session.dropped_records &&
                records >= expected_dropped_records &&
                timestamp_ok(timestamp, last_timestamp, have_timestamp);
        if (valid) {
          expected_dropped_records = records;
          record = CaptureRecord::statistics_record(
              StatisticsRecord{timestamp, stats_segment, dropped, records});
        }
      } else {
        static const char *names[] = {"t_us", "bus", "segment", "reason"};
        valid = required(map, names, 4, error) && get_number(map, "t_us", timestamp);
        std::uint64_t next_segment = 0;
        bool all = false;
        std::uint8_t bus_id = 0;
        std::string reason;
        valid = valid && bus(map["bus"], all, bus_id) && get_number(map, "segment", next_segment) &&
                next_segment > segment && decode_percent(map["reason"], reason) &&
                timestamp_ok(timestamp, last_timestamp, have_timestamp);
        if (valid) {
          record = CaptureRecord::discontinuity_record(
              DiscontinuityRecord{timestamp, all, bus_id, next_segment, std::move(reason)});
          segment = next_segment;
          starts_new_segment = true;
        }
      }
      if (valid) {
        result.records.push_back(std::move(record));
        last_timestamp = timestamp;
        have_timestamp = !starts_new_segment;
      } else {
        report(line_number, error.empty() ? "invalid record value or ordering" : error, line);
        if (mode == ParseMode::Strict) {
          result.ok = false;
          return result;
        }
      }
    }
    if (end == std::string_view::npos)
      break;
    start = end + 1;
  }
  if (!session_seen)
    result.ok = false;
  return result;
}

} // namespace raw_capture
