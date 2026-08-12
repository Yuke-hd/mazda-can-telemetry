#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "raw_capture/capture_reader.hpp"

namespace raw_capture {

class CaptureWriter final {
public:
  [[nodiscard]] static bool write_session(const SessionMetadata &session, std::string &output,
                                          std::string *error = nullptr);
  [[nodiscard]] static bool write_record(const CaptureRecord &record, std::string &output,
                                         std::string *error = nullptr);
  [[nodiscard]] static bool write(const SessionMetadata &session,
                                  const std::vector<CaptureRecord> &records, std::string &output,
                                  std::string *error = nullptr);
};

} // namespace raw_capture
