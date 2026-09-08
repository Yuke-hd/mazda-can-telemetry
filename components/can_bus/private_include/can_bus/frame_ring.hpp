#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "can_bus/can_bus.h"

namespace can_bus::internal {

enum class RingIndex : std::uint8_t {
  kHead,
  kTail,
};

// Read the consumer-owned index on both sides of the producer-owned index.
// A statistics observer can be delayed after reading head while pop() advances
// tail; accepting those independent reads would make unsigned subtraction
// report a depth near SIZE_MAX. The retry also rejects any sample whose
// distance is outside the fixed ring capacity instead of hiding a bad sample
// by clamping it. Load must return the corresponding monotonic ring index.
template <std::size_t Capacity, typename Load>
[[nodiscard]] std::uint32_t coherent_depth(Load &&load) noexcept {
  for (;;) {
    const std::size_t tail_before = load(RingIndex::kTail);
    const std::size_t head = load(RingIndex::kHead);
    const std::size_t tail_after = load(RingIndex::kTail);
    if (tail_before != tail_after) {
      continue;
    }

    const std::size_t distance = head - tail_after;
    if (distance <= Capacity) {
      return static_cast<std::uint32_t>(distance);
    }
  }
}

// A fixed-capacity SPSC boundary. The producer always drops the newest frame
// when full, so an absent or slow consumer can never block acquisition. The
// public receive API documents the single-consumer ownership requirement.
template <std::size_t Capacity> class FrameRing final {
  static_assert(Capacity > 0, "the acquisition ring must have storage");

public:
  [[nodiscard]] bool push(const vehicle_core::RawCanFrame &frame) noexcept {
    frames_received_.fetch_add(1, std::memory_order_relaxed);
    const std::size_t head = head_.load(std::memory_order_relaxed);
    const std::size_t tail = tail_.load(std::memory_order_acquire);
    if ((head - tail) >= Capacity) {
      frames_dropped_.fetch_add(1, std::memory_order_relaxed);
      queue_overflows_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }

    frames_[head % Capacity] = frame;
    // Publish head before the watermark. A reset that observes this head can
    // then reconcile the watermark even if it wins the following exchange;
    // a producer that publishes after reconciliation raises it afterward.
    head_.store(head + 1, std::memory_order_release);
    update_watermark(static_cast<std::uint32_t>((head + 1) - tail));
    frames_queued_.fetch_add(1, std::memory_order_relaxed);
    return true;
  }

  [[nodiscard]] bool pop(vehicle_core::RawCanFrame &frame) noexcept {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    const std::size_t head = head_.load(std::memory_order_acquire);
    if (tail == head) {
      return false;
    }
    frame = frames_[tail % Capacity];
    tail_.store(tail + 1, std::memory_order_release);
    frames_delivered_.fetch_add(1, std::memory_order_relaxed);
    return true;
  }

  void record_bus_error(std::uint64_t count = 1) noexcept {
    bus_errors_.fetch_add(count, std::memory_order_relaxed);
  }

  void record_driver_rx_missed(std::uint64_t count = 1) noexcept {
    driver_rx_missed_.fetch_add(count, std::memory_order_relaxed);
  }

  void record_controller_reset(std::uint64_t count = 1) noexcept {
    controller_resets_.fetch_add(count, std::memory_order_relaxed);
  }

  void record_bus_off(std::uint64_t count = 1) noexcept {
    bus_off_.fetch_add(count, std::memory_order_relaxed);
  }

  [[nodiscard]] Statistics snapshot(const StatisticsOperation operation) noexcept {
    // Each counter is an independent atomic observation. A returned snapshot
    // is therefore not a transactional cross-counter report: activity racing
    // a reset may be attributed to either adjacent interval. Queue indices are
    // sampled coherently below, so depth and watermark remain bounded.
    Statistics result{};
    result.queue_depth = depth();
    result.queue_capacity = static_cast<std::uint32_t>(Capacity);

    if (operation == StatisticsOperation::kSnapshotAndReset) {
      result.frames_received = frames_received_.exchange(0, std::memory_order_relaxed);
      result.frames_queued = frames_queued_.exchange(0, std::memory_order_relaxed);
      result.frames_delivered = frames_delivered_.exchange(0, std::memory_order_relaxed);
      result.frames_dropped = frames_dropped_.exchange(0, std::memory_order_relaxed);
      result.queue_overflows = queue_overflows_.exchange(0, std::memory_order_relaxed);
      result.bus_errors = bus_errors_.exchange(0, std::memory_order_relaxed);
      result.driver_rx_missed = driver_rx_missed_.exchange(0, std::memory_order_relaxed);
      result.controller_resets = controller_resets_.exchange(0, std::memory_order_relaxed);
      result.bus_off = bus_off_.exchange(0, std::memory_order_relaxed);
      result.queue_high_watermark =
          queue_high_watermark_.exchange(result.queue_depth, std::memory_order_relaxed);
      // A producer may have published a frame after the initial depth read.
      // Reconcile with a fresh depth after the reset so the next interval's
      // watermark cannot be below the live queue depth.
      ensure_watermark_at_least(depth());
    } else {
      result.frames_received = frames_received_.load(std::memory_order_relaxed);
      result.frames_queued = frames_queued_.load(std::memory_order_relaxed);
      result.frames_delivered = frames_delivered_.load(std::memory_order_relaxed);
      result.frames_dropped = frames_dropped_.load(std::memory_order_relaxed);
      result.queue_overflows = queue_overflows_.load(std::memory_order_relaxed);
      result.bus_errors = bus_errors_.load(std::memory_order_relaxed);
      result.driver_rx_missed = driver_rx_missed_.load(std::memory_order_relaxed);
      result.controller_resets = controller_resets_.load(std::memory_order_relaxed);
      result.bus_off = bus_off_.load(std::memory_order_relaxed);
      result.queue_high_watermark = queue_high_watermark_.load(std::memory_order_relaxed);
    }
    return result;
  }

  void clear() noexcept {
    const std::size_t head = head_.load(std::memory_order_acquire);
    tail_.store(head, std::memory_order_release);
    (void)snapshot(StatisticsOperation::kSnapshotAndReset);
    queue_high_watermark_.store(0, std::memory_order_relaxed);
  }

private:
  [[nodiscard]] std::uint32_t depth() const noexcept {
    return coherent_depth<Capacity>([this](const RingIndex index) noexcept {
      return index == RingIndex::kHead ? head_.load(std::memory_order_acquire)
                                       : tail_.load(std::memory_order_acquire);
    });
  }

  void update_watermark(const std::uint32_t depth_value) noexcept {
    std::uint32_t watermark = queue_high_watermark_.load(std::memory_order_relaxed);
    while (depth_value > watermark && !queue_high_watermark_.compare_exchange_weak(
                                          watermark, depth_value, std::memory_order_relaxed)) {
    }
  }

  void ensure_watermark_at_least(const std::uint32_t depth_value) noexcept {
    std::uint32_t watermark = queue_high_watermark_.load(std::memory_order_relaxed);
    while (watermark < depth_value && !queue_high_watermark_.compare_exchange_weak(
                                          watermark, depth_value, std::memory_order_relaxed)) {
    }
  }

  std::array<vehicle_core::RawCanFrame, Capacity> frames_{};
  std::atomic<std::size_t> head_{0};
  std::atomic<std::size_t> tail_{0};
  std::atomic<std::uint64_t> frames_received_{0};
  std::atomic<std::uint64_t> frames_queued_{0};
  std::atomic<std::uint64_t> frames_delivered_{0};
  std::atomic<std::uint64_t> frames_dropped_{0};
  std::atomic<std::uint64_t> queue_overflows_{0};
  std::atomic<std::uint64_t> bus_errors_{0};
  std::atomic<std::uint64_t> driver_rx_missed_{0};
  std::atomic<std::uint64_t> controller_resets_{0};
  std::atomic<std::uint64_t> bus_off_{0};
  std::atomic<std::uint32_t> queue_high_watermark_{0};
};

} // namespace can_bus::internal
