#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "raw_capture/capture_reader.hpp"

namespace raw_capture {

class SimulatedMonotonicClock final : public vehicle_core::MonotonicClock {
public:
  [[nodiscard]] vehicle_core::MonotonicTimestamp now() const noexcept override { return now_us_; }
  // set() is monotonic; reset() is reserved for a capture discontinuity.
  void set(vehicle_core::MonotonicTimestamp timestamp_us) noexcept {
    if (timestamp_us >= now_us_)
      now_us_ = timestamp_us;
  }
  void reset(vehicle_core::MonotonicTimestamp timestamp_us) noexcept { now_us_ = timestamp_us; }
  void advance(std::uint64_t delta_us) noexcept;
  void pause() noexcept { paused_ = true; }
  void resume() noexcept { paused_ = false; }
  [[nodiscard]] bool paused() const noexcept { return paused_; }

private:
  vehicle_core::MonotonicTimestamp now_us_{0};
  bool paused_{false};
};

using ReplayClock = SimulatedMonotonicClock;

class ReplayHarness final {
public:
  using FrameHandler =
      std::function<void(const vehicle_core::RawCanFrame &, SimulatedMonotonicClock &)>;

  explicit ReplayHarness(SimulatedMonotonicClock &clock) noexcept : clock_(&clock) {}
  void load(const std::vector<CaptureRecord> &records);
  void pause() noexcept { clock_->pause(); }
  void resume() noexcept { clock_->resume(); }
  [[nodiscard]] std::size_t advance_to(vehicle_core::MonotonicTimestamp timestamp_us,
                                       const FrameHandler &handler);
  [[nodiscard]] std::size_t advance_by(std::uint64_t delta_us, const FrameHandler &handler);
  [[nodiscard]] bool pending() const noexcept { return next_record_ < records_.size(); }
  void replay(const std::vector<CaptureRecord> &records, const FrameHandler &handler);

private:
  SimulatedMonotonicClock *clock_;
  std::vector<CaptureRecord> records_;
  std::size_t next_record_{0};
};

} // namespace raw_capture
