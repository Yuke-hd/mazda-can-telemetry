#include "raw_capture/replay.hpp"

#include <limits>

namespace raw_capture {
void SimulatedMonotonicClock::advance(const std::uint64_t delta_us) noexcept {
  if (paused_ || delta_us > std::numeric_limits<std::uint64_t>::max() - now_us_)
    return;
  now_us_ += delta_us;
}

void ReplayHarness::load(const std::vector<CaptureRecord> &records) {
  records_ = records;
  next_record_ = 0;
  clock_->reset(0);
}

std::size_t ReplayHarness::advance_to(const vehicle_core::MonotonicTimestamp timestamp_us,
                                      const FrameHandler &handler) {
  if (!handler || clock_->paused() || timestamp_us < clock_->now())
    return 0;
  std::size_t delivered = 0;
  while (next_record_ < records_.size()) {
    const auto &record = records_[next_record_];
    if (record.type == RecordType::Discontinuity) {
      if (record.discontinuity.timestamp_us > timestamp_us)
        break;
      clock_->reset(record.discontinuity.timestamp_us);
      ++next_record_;
      continue;
    }
    if (record.type != RecordType::Frame) {
      ++next_record_;
      continue;
    }
    if (record.frame.timestamp_us > timestamp_us)
      break;
    clock_->set(record.frame.timestamp_us);
    handler(record.frame, *clock_);
    ++next_record_;
    ++delivered;
  }
  clock_->set(timestamp_us);
  return delivered;
}

std::size_t ReplayHarness::advance_by(const std::uint64_t delta_us, const FrameHandler &handler) {
  if (clock_->paused() || delta_us > std::numeric_limits<std::uint64_t>::max() - clock_->now())
    return 0;
  return advance_to(clock_->now() + delta_us, handler);
}

void ReplayHarness::replay(const std::vector<CaptureRecord> &records,
                           const FrameHandler &handler) const {
  if (!handler)
    return;
  auto *self = const_cast<ReplayHarness *>(this);
  self->load(records);
  self->resume();
  (void)self->advance_to(std::numeric_limits<std::uint64_t>::max(), handler);
}
} // namespace raw_capture
