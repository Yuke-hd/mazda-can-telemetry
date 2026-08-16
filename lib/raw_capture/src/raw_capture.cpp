#include "raw_capture/exporter.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace raw_capture {
namespace {

constexpr char kHex[] = "0123456789abcdef";
constexpr char kHexUpper[] = "0123456789ABCDEF";
constexpr std::size_t kLineCapacity = 512;

class Line final {
public:
  [[nodiscard]] bool append(const char value) noexcept {
    if (size_ >= bytes_.size()) {
      return false;
    }
    bytes_[size_++] = value;
    return true;
  }

  [[nodiscard]] bool append(std::string_view value) noexcept {
    if (value.size() > bytes_.size() - size_) {
      return false;
    }
    std::copy(value.begin(), value.end(), bytes_.begin() + static_cast<std::ptrdiff_t>(size_));
    size_ += value.size();
    return true;
  }

  [[nodiscard]] bool append_uint(const std::uint64_t value) noexcept {
    char reversed[20]{};
    std::size_t count = 0;
    std::uint64_t remaining = value;
    do {
      reversed[count++] = static_cast<char>('0' + (remaining % 10));
      remaining /= 10;
    } while (remaining != 0);
    while (count != 0) {
      if (!append(reversed[--count])) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] bool append_hex(const std::uint32_t value, const std::size_t digits) noexcept {
    for (std::size_t index = digits; index != 0; --index) {
      if (!append(kHex[(value >> ((index - 1) * 4)) & 0x0fU])) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] bool append_percent_encoded(const std::string_view value) noexcept {
    for (const unsigned char byte : value) {
      const bool unreserved = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
                              (byte >= '0' && byte <= '9') || byte == '-' || byte == '.' ||
                              byte == '_' || byte == '~';
      if (unreserved) {
        if (!append(static_cast<char>(byte))) {
          return false;
        }
      } else if (!append('%') || !append(kHexUpper[byte >> 4]) || !append(kHexUpper[byte & 0x0f])) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] bool finish() noexcept { return append('\n'); }
  [[nodiscard]] std::string_view view() const noexcept {
    return std::string_view(bytes_.data(), size_);
  }

private:
  std::array<char, kLineCapacity> bytes_{};
  std::size_t size_{0};
};

[[nodiscard]] bool append_frame(Line &line, const vehicle_core::RawCanFrame &frame) noexcept {
  if (!frame.is_valid() ||
      (frame.identifier_format != vehicle_core::CanIdentifierFormat::Standard &&
       frame.identifier_format != vehicle_core::CanIdentifierFormat::Extended)) {
    return false;
  }
  const std::size_t id_digits = frame.is_extended() ? 8 : 3;
  if (!line.append("FRAME t_us=") || !line.append_uint(frame.timestamp_us) ||
      !line.append(" bus=") || !line.append_uint(frame.bus_id) || !line.append(" id=0x") ||
      !line.append_hex(frame.identifier, id_digits) ||
      !line.append(frame.is_extended() ? " format=ext rtr=" : " format=std rtr=") ||
      !line.append_uint(frame.remote_request ? 1 : 0) || !line.append(" dlc=") ||
      !line.append_uint(frame.dlc) || !line.append(" data=")) {
    return false;
  }
  if (frame.remote_request || frame.dlc == 0) {
    if (!line.append('-')) {
      return false;
    }
  } else {
    for (std::size_t index = 0; index < frame.dlc; ++index) {
      if (!line.append(kHex[frame.data[index] >> 4]) ||
          !line.append(kHex[frame.data[index] & 0x0f])) {
        return false;
      }
    }
  }
  return line.finish();
}

} // namespace

bool Exporter::Queue::push(const vehicle_core::RawCanFrame &frame) noexcept {
  if ((head - tail) >= kFrameQueueCapacity) {
    return false;
  }
  frames[head % kFrameQueueCapacity].frame = frame;
  ++head;
  const auto depth_now = depth();
  high_watermark = std::max(high_watermark, depth_now);
  return true;
}

bool Exporter::Queue::pop(vehicle_core::RawCanFrame &frame) noexcept {
  if (tail == head) {
    return false;
  }
  frame = frames[tail % kFrameQueueCapacity].frame;
  ++tail;
  return true;
}

std::uint32_t Exporter::Queue::depth() const noexcept {
  return static_cast<std::uint32_t>(head - tail);
}

bool Exporter::valid_utf8(const std::string_view value) noexcept {
  for (std::size_t index = 0; index < value.size();) {
    const auto lead = static_cast<unsigned char>(value[index]);
    if (lead <= 0x7fU) {
      ++index;
      continue;
    }
    const std::size_t width = lead >= 0xf0U && lead <= 0xf4U   ? 4
                              : lead >= 0xe0U && lead <= 0xefU ? 3
                              : lead >= 0xc2U && lead <= 0xdfU ? 2
                                                               : 0;
    if (width == 0 || index + width > value.size()) {
      return false;
    }
    for (std::size_t continuation = 1; continuation < width; ++continuation) {
      if ((static_cast<unsigned char>(value[index + continuation]) & 0xc0U) != 0x80U) {
        return false;
      }
    }
    const auto second = static_cast<unsigned char>(value[index + 1]);
    if ((lead == 0xe0U && second < 0xa0U) || (lead == 0xedU && second >= 0xa0U) ||
        (lead == 0xf0U && second < 0x90U) || (lead == 0xf4U && second >= 0x90U)) {
      return false;
    }
    index += width;
  }
  return true;
}

bool Exporter::copy_text(std::array<char, 128> &destination, std::size_t &size,
                         const std::string_view value) noexcept {
  if (value.empty() || value.size() >= destination.size() || !valid_utf8(value)) {
    return false;
  }
  std::copy(value.begin(), value.end(), destination.begin());
  size = value.size();
  return true;
}

Exporter::Exporter(Configuration configuration) noexcept : configuration_(configuration) {
  dropped_frames_ = configuration_.session.dropped_frames;
  dropped_records_ = configuration_.session.dropped_records;
  if (configuration_.session.bitrate_bps == 0 || configuration_.session.clock_hz == 0 ||
      configuration_.session.firmware.size() * 3U + configuration_.session.board.size() * 3U >
          300U ||
      !copy_text(firmware_, firmware_size_, configuration_.session.firmware) ||
      !copy_text(board_, board_size_, configuration_.session.board) ||
      !copy_text(pending_drop_reason_storage_, pending_drop_reason_size_,
                 "export-queue-overflow")) {
    failed_ = true;
  } else {
    // Keep the public configuration self-contained even when the caller's
    // original string views referred to temporary storage.
    configuration_.session.firmware = std::string_view(firmware_.data(), firmware_size_);
    configuration_.session.board = std::string_view(board_.data(), board_size_);
  }
}

std::size_t Exporter::poll_input(FrameSource &source, const std::size_t max_frames) noexcept {
  std::size_t consumed = 0;
  while (consumed < max_frames) {
    vehicle_core::RawCanFrame frame{};
    if (!source.try_receive(frame)) {
      break;
    }
    ++consumed;
    ++input_frames_;
    if (!frame.is_valid() ||
        (frame.identifier_format != vehicle_core::CanIdentifierFormat::Standard &&
         frame.identifier_format != vehicle_core::CanIdentifierFormat::Extended)) {
      record_drop(1, frame.timestamp_us, frame.bus_id, "invalid-frame");
    } else if (!queue_.push(frame)) {
      ++queue_overflows_;
      record_drop(1, frame.timestamp_us, frame.bus_id, "export-queue-overflow");
    } else {
      queue_.frames[(queue_.head + kFrameQueueCapacity - 1) % kFrameQueueCapacity].segment =
          capture_segment_;
      ++queued_frames_;
    }
  }
  return consumed;
}

void Exporter::record_drop(const std::uint64_t count, const std::uint64_t timestamp_us,
                           const std::uint8_t bus_id, const std::string_view reason) noexcept {
  dropped_frames_ += count;
  if (drop_head_ != drop_tail_) {
    auto &previous =
        drop_boundaries_[(drop_head_ + kDropBoundaryCapacity - 1) % kDropBoundaryCapacity];
    const std::string_view previous_reason(previous.reason.data(), previous.reason_size);
    if (previous.segment == capture_segment_ && previous.bus == bus_id &&
        previous_reason == reason && previous.frame_boundary == queue_.head) {
      previous.count += count;
      if (drop_head_ - drop_tail_ == 1) {
        pending_drop_frames_ = previous.count;
        pending_drop_timestamp_us_ = previous.timestamp_us;
      }
      return;
    }
  }
  if (drop_head_ - drop_tail_ >= kDropBoundaryCapacity) {
    ++dropped_records_;
    return;
  }
  auto &boundary = drop_boundaries_[drop_head_ % kDropBoundaryCapacity];
  boundary.count = count;
  boundary.timestamp_us = timestamp_us;
  boundary.segment = capture_segment_;
  boundary.frame_boundary = queue_.head;
  boundary.bus = bus_id;
  const auto bounded_reason = reason.size() < boundary.reason.size() ? reason : "invalid-reason";
  std::copy(bounded_reason.begin(), bounded_reason.end(), boundary.reason.begin());
  boundary.reason_size = bounded_reason.size();
  ++drop_head_;
  if (drop_head_ - drop_tail_ == 1) {
    pending_drop_frames_ = boundary.count;
    pending_drop_timestamp_us_ = boundary.timestamp_us;
    pending_drop_segment_ = boundary.segment;
    pending_drop_bus_ = boundary.bus;
    std::copy(boundary.reason.begin(),
              boundary.reason.begin() + static_cast<std::ptrdiff_t>(boundary.reason_size),
              pending_drop_reason_storage_.begin());
    pending_drop_reason_size_ = boundary.reason_size;
  }
}

void Exporter::note_dropped_frames(const std::uint64_t count, const std::uint64_t timestamp_us,
                                   const std::uint8_t bus_id,
                                   const std::string_view reason) noexcept {
  if (count != 0) {
    record_drop(count, timestamp_us, bus_id, reason);
  }
}

Exporter::Attempt Exporter::write_line(OutputSink &sink, const std::string_view line) noexcept {
  switch (sink.write(line)) {
  case WriteResult::kWritten:
    return Attempt::kWritten;
  case WriteResult::kWouldBlock:
    return Attempt::kDeferred;
  case WriteResult::kDisconnected:
    sink.discard_partial_line();
    if (connection_latched_) {
      connection_latched_ = false;
      if (session_emitted_) {
        ++segment_;
        capture_segment_ = segment_;
        reconnect_pending_ = true;
      }
    }
    return Attempt::kDeferred;
  case WriteResult::kError:
    failed_ = true;
    ++dropped_records_;
    return Attempt::kFailed;
  }
  failed_ = true;
  ++dropped_records_;
  return Attempt::kFailed;
}

Exporter::Attempt Exporter::write_header(OutputSink &sink) noexcept {
  return write_line(sink, "MCAN-CAPTURE 1\n");
}

Exporter::Attempt Exporter::write_session(OutputSink &sink) noexcept {
  Line line;
  if (!line.append("SESSION firmware=") ||
      !line.append_percent_encoded(std::string_view(firmware_.data(), firmware_size_)) ||
      !line.append(" board=") ||
      !line.append_percent_encoded(std::string_view(board_.data(), board_size_)) ||
      !line.append(" bitrate_bps=") || !line.append_uint(configuration_.session.bitrate_bps) ||
      !line.append(" clock=monotonic clock_unit=us byte_order=big-endian clock_hz=") ||
      !line.append_uint(configuration_.session.clock_hz) || !line.append(" dropped_frames=") ||
      !line.append_uint(configuration_.session.dropped_frames) ||
      !line.append(" dropped_records=") ||
      !line.append_uint(configuration_.session.dropped_records) || !line.finish()) {
    failed_ = true;
    return Attempt::kFailed;
  }
  return write_line(sink, line.view());
}

bool Exporter::diagnostic_allowed(const std::uint64_t now_us) const noexcept {
  return now_us >= next_diagnostic_us_;
}

void Exporter::schedule_next_diagnostic(const std::uint64_t now_us) noexcept {
  const auto interval = configuration_.diagnostic_interval_us;
  next_diagnostic_us_ = interval > std::numeric_limits<std::uint64_t>::max() - now_us
                            ? std::numeric_limits<std::uint64_t>::max()
                            : now_us + interval;
}

Exporter::Attempt Exporter::write_discontinuity(OutputSink &sink,
                                                const std::uint64_t now_us) noexcept {
  Line line;
  if (!line.append("DISCONTINUITY t_us=") || !line.append_uint(now_us) ||
      !line.append(" bus=all segment=") || !line.append_uint(segment_) ||
      !line.append(" reason=usb-reconnect") || !line.finish()) {
    failed_ = true;
    return Attempt::kFailed;
  }
  const auto result = write_line(sink, line.view());
  if (result == Attempt::kWritten) {
    reconnect_pending_ = false;
    ++emitted_diagnostics_;
    schedule_next_diagnostic(now_us);
  }
  return result;
}

Exporter::Attempt Exporter::write_drop(OutputSink &sink, const std::uint64_t now_us) noexcept {
  Line line;
  if (!line.append("DROP t_us=") || !line.append_uint(pending_drop_timestamp_us_) ||
      !line.append(" bus=") ||
      !(pending_drop_bus_ == 0xff ? line.append("all") : line.append_uint(pending_drop_bus_)) ||
      !line.append(" count=") || !line.append_uint(pending_drop_frames_) ||
      !line.append(" reason=") ||
      !line.append_percent_encoded(
          std::string_view(pending_drop_reason_storage_.data(), pending_drop_reason_size_)) ||
      !line.finish()) {
    failed_ = true;
    return Attempt::kFailed;
  }
  const auto result = write_line(sink, line.view());
  if (result == Attempt::kWritten) {
    ++drop_tail_;
    if (drop_tail_ != drop_head_) {
      const auto &next = drop_boundaries_[drop_tail_ % kDropBoundaryCapacity];
      pending_drop_frames_ = next.count;
      pending_drop_timestamp_us_ = next.timestamp_us;
      pending_drop_segment_ = next.segment;
      pending_drop_bus_ = next.bus;
      pending_drop_reason_size_ = next.reason_size;
      std::copy(next.reason.begin(),
                next.reason.begin() + static_cast<std::ptrdiff_t>(next.reason_size),
                pending_drop_reason_storage_.begin());
    } else {
      pending_drop_frames_ = 0;
    }
    ++emitted_diagnostics_;
    schedule_next_diagnostic(now_us);
  }
  return result;
}

Exporter::Attempt Exporter::write_statistics(OutputSink &sink,
                                             const std::uint64_t now_us) noexcept {
  Line line;
  if (!line.append("STATS t_us=") || !line.append_uint(now_us) || !line.append(" segment=") ||
      !line.append_uint(segment_) || !line.append(" dropped_frames=") ||
      !line.append_uint(dropped_frames_) || !line.append(" dropped_records=") ||
      !line.append_uint(dropped_records_) || !line.finish()) {
    failed_ = true;
    return Attempt::kFailed;
  }
  const auto result = write_line(sink, line.view());
  if (result == Attempt::kWritten) {
    final_statistics_pending_ = false;
    next_statistics_us_ =
        configuration_.statistics_interval_us == 0 ? std::numeric_limits<std::uint64_t>::max()
        : configuration_.statistics_interval_us > std::numeric_limits<std::uint64_t>::max() - now_us
            ? std::numeric_limits<std::uint64_t>::max()
            : now_us + configuration_.statistics_interval_us;
    ++emitted_diagnostics_;
  }
  return result;
}

Exporter::Attempt Exporter::write_frame(OutputSink &sink) noexcept {
  if (queue_.depth() == 0) {
    return Attempt::kDeferred;
  }
  const auto &item = queue_.frames[queue_.tail % kFrameQueueCapacity];
  const auto &frame = item.frame;
  Line line;
  if (!append_frame(line, frame)) {
    failed_ = true;
    return Attempt::kFailed;
  }
  const auto result = write_line(sink, line.view());
  if (result == Attempt::kWritten) {
    vehicle_core::RawCanFrame discarded{};
    (void)queue_.pop(discarded);
    ++emitted_frames_;
  } else if (result == Attempt::kFailed) {
    vehicle_core::RawCanFrame discarded{};
    (void)queue_.pop(discarded);
    record_drop(1, frame.timestamp_us, frame.bus_id, "output-error");
  }
  return result;
}

std::size_t Exporter::poll_output(OutputSink &sink, const std::uint64_t now_us,
                                  const std::size_t max_records) noexcept {
  if (failed_ || max_records == 0) {
    return 0;
  }
  if (!sink.connected()) {
    sink.discard_partial_line();
    if (connection_latched_) {
      connection_latched_ = false;
      if (session_emitted_) {
        ++segment_;
        capture_segment_ = segment_;
        reconnect_pending_ = true;
      }
    }
    return 0;
  }
  if (!connection_latched_) {
    connection_latched_ = true;
  }

  std::size_t written = 0;
  while (written < max_records && !failed_) {
    Attempt attempt = Attempt::kDeferred;
    if (!header_emitted_) {
      attempt = write_header(sink);
      if (attempt == Attempt::kWritten) {
        header_emitted_ = true;
      }
    } else if (!session_emitted_) {
      attempt = write_session(sink);
      if (attempt == Attempt::kWritten) {
        session_emitted_ = true;
        next_statistics_us_ = configuration_.statistics_interval_us == 0
                                  ? std::numeric_limits<std::uint64_t>::max()
                              : configuration_.statistics_interval_us >
                                      std::numeric_limits<std::uint64_t>::max() - now_us
                                  ? std::numeric_limits<std::uint64_t>::max()
                                  : now_us + configuration_.statistics_interval_us;
      }
    } else if (reconnect_pending_) {
      if (pending_drop_frames_ != 0 && pending_drop_segment_ < segment_ &&
          (queue_.depth() == 0 ||
           queue_.tail >= drop_boundaries_[drop_tail_ % kDropBoundaryCapacity].frame_boundary)) {
        attempt = write_drop(sink, now_us);
      } else if (queue_.depth() != 0 &&
                 queue_.frames[queue_.tail % kFrameQueueCapacity].segment < segment_) {
        attempt = write_frame(sink);
      } else {
        attempt = write_discontinuity(sink, now_us);
      }
    } else if (pending_drop_frames_ != 0) {
      if (!diagnostic_allowed(now_us)) {
        if (queue_.depth() != 0 &&
            queue_.tail < drop_boundaries_[drop_tail_ % kDropBoundaryCapacity].frame_boundary) {
          attempt = write_frame(sink);
        } else {
          break;
        }
      } else if (queue_.depth() == 0 ||
                 queue_.tail >=
                     drop_boundaries_[drop_tail_ % kDropBoundaryCapacity].frame_boundary) {
        attempt = write_drop(sink, now_us);
      } else {
        attempt = write_frame(sink);
      }
    } else if (queue_.depth() != 0) {
      attempt = write_frame(sink);
    } else if (final_statistics_pending_ || now_us >= next_statistics_us_) {
      attempt = write_statistics(sink, now_us);
    } else {
      break;
    }
    if (attempt == Attempt::kWritten) {
      ++written;
    } else {
      break;
    }
  }
  return written;
}

Statistics Exporter::statistics() const noexcept {
  return Statistics{input_frames_,        queued_frames_,   emitted_frames_,
                    dropped_frames_,      dropped_records_, queue_overflows_,
                    emitted_diagnostics_, queue_.depth(),   queue_.high_watermark};
}

} // namespace raw_capture
