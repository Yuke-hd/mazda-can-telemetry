#include "raw_capture/capture_writer.hpp"

namespace raw_capture {
namespace {
const char *const kHex = "0123456789abcdef";

void append_percent(const std::string_view value, std::string &output) {
  for (const unsigned char byte : value) {
    if ((byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
        (byte >= '0' && byte <= '9') || byte == '-' || byte == '.' || byte == '_' || byte == '~')
      output.push_back(static_cast<char>(byte));
    else {
      output.push_back('%');
      output.push_back(kHex[byte >> 4U]);
      output.push_back(kHex[byte & 0x0fU]);
    }
  }
}
template <typename T> void append_decimal(const T value, std::string &output) {
  output += std::to_string(value);
}
bool valid_text(const std::string_view value) {
  if (value.empty())
    return false;
  for (std::size_t i = 0; i < value.size();) {
    const auto lead = static_cast<unsigned char>(value[i]);
    const std::size_t width = lead <= 0x7fU                    ? 1
                              : lead >= 0xf0U && lead <= 0xf4U ? 4
                              : lead >= 0xe0U && lead <= 0xefU ? 3
                              : lead >= 0xc2U && lead <= 0xdfU ? 2
                                                               : 0;
    if (width == 0 || i + width > value.size())
      return false;
    for (std::size_t j = 1; j < width; ++j)
      if ((static_cast<unsigned char>(value[i + j]) & 0xc0U) != 0x80U)
        return false;
    if (width > 1) {
      const auto second = static_cast<unsigned char>(value[i + 1]);
      if ((lead == 0xe0U && second < 0xa0U) || (lead == 0xedU && second >= 0xa0U) ||
          (lead == 0xf0U && second < 0x90U) || (lead == 0xf4U && second >= 0x90U))
        return false;
    }
    i += width;
  }
  for (const unsigned char c : value)
    if (c == 0 || c == '\n' || c == '\r' || c == ' ')
      return false;
  return true;
}
bool fail(std::string *error, const char *message) {
  if (error)
    *error = message;
  return false;
}
} // namespace

bool CaptureWriter::write_session(const SessionMetadata &session, std::string &output,
                                  std::string *error) {
  if (!valid_text(session.firmware) || !valid_text(session.board) || session.bitrate_bps == 0 ||
      session.clock_hz == 0)
    return fail(error, "invalid session metadata");
  output += "MCAN-CAPTURE 1\nSESSION firmware=";
  append_percent(session.firmware, output);
  output += " board=";
  append_percent(session.board, output);
  output += " bitrate_bps=";
  append_decimal(session.bitrate_bps, output);
  output += " clock=monotonic clock_unit=us byte_order=big-endian clock_hz=";
  append_decimal(session.clock_hz, output);
  output += " dropped_frames=";
  append_decimal(session.dropped_frames, output);
  output += " dropped_records=";
  append_decimal(session.dropped_records, output);
  output.push_back('\n');
  return true;
}

bool CaptureWriter::write_record(const CaptureRecord &record, std::string &output,
                                 std::string *error) {
  switch (record.type) {
  case RecordType::Frame: {
    const auto &frame = record.frame;
    if (!frame.is_valid() ||
        (frame.identifier_format != vehicle_core::CanIdentifierFormat::Standard &&
         frame.identifier_format != vehicle_core::CanIdentifierFormat::Extended))
      return fail(error, "invalid frame");
    output += "FRAME t_us=";
    append_decimal(frame.timestamp_us, output);
    output += " bus=";
    append_decimal(frame.bus_id, output);
    output += " id=0x";
    const auto digits = frame.is_extended() ? 8U : 3U;
    for (int i = static_cast<int>(digits) - 1; i >= 0; --i)
      output.push_back(kHex[(frame.identifier >> (i * 4)) & 0xfU]);
    output += frame.is_extended() ? " format=ext rtr=" : " format=std rtr=";
    append_decimal(frame.remote_request ? 1 : 0, output);
    output += " dlc=";
    append_decimal(frame.dlc, output);
    output += " data=";
    if (frame.remote_request || frame.dlc == 0)
      output.push_back('-');
    else
      for (std::size_t i = 0; i < frame.dlc; ++i) {
        output.push_back(kHex[frame.data[i] >> 4U]);
        output.push_back(kHex[frame.data[i] & 0xfU]);
      }
    output.push_back('\n');
    return true;
  }
  case RecordType::Drop:
    if (record.drop.count == 0 || !valid_text(record.drop.reason))
      return fail(error, "invalid drop record");
    output += "DROP t_us=";
    append_decimal(record.drop.timestamp_us, output);
    output += " bus=";
    if (record.drop.all_buses)
      output += "all";
    else
      append_decimal(record.drop.bus_id, output);
    output += " count=";
    append_decimal(record.drop.count, output);
    output += " reason=";
    append_percent(record.drop.reason, output);
    output.push_back('\n');
    return true;
  case RecordType::Statistics:
    output += "STATS t_us=";
    append_decimal(record.statistics.timestamp_us, output);
    output += " segment=";
    append_decimal(record.statistics.segment, output);
    output += " dropped_frames=";
    append_decimal(record.statistics.dropped_frames, output);
    output += " dropped_records=";
    append_decimal(record.statistics.dropped_records, output);
    output.push_back('\n');
    return true;
  case RecordType::Discontinuity:
    if (record.discontinuity.segment == 0 || !valid_text(record.discontinuity.reason))
      return fail(error, "invalid discontinuity record");
    output += "DISCONTINUITY t_us=";
    append_decimal(record.discontinuity.timestamp_us, output);
    output += " bus=";
    if (record.discontinuity.all_buses)
      output += "all";
    else
      append_decimal(record.discontinuity.bus_id, output);
    output += " segment=";
    append_decimal(record.discontinuity.segment, output);
    output += " reason=";
    append_percent(record.discontinuity.reason, output);
    output.push_back('\n');
    return true;
  }
  return fail(error, "unknown record type");
}

bool CaptureWriter::write(const SessionMetadata &session, const std::vector<CaptureRecord> &records,
                          std::string &output, std::string *error) {
  output.clear();
  if (!write_session(session, output, error))
    return false;
  for (const auto &record : records)
    if (!write_record(record, output, error)) {
      output.clear();
      return false;
    }
  return true;
}
} // namespace raw_capture
