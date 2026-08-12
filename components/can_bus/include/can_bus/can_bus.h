#pragma once

#include <cstdint>

#include "vehicle_core/vehicle_core.hpp"

namespace can_bus {

inline constexpr std::uint32_t kQueueCapacity = 64;

enum class Result : std::uint8_t {
  kOk,
  kInvalidConfiguration,
  kAlreadyStarted,
  kNotStarted,
  kTimeout,
  kDriverFailure,
  kTaskFailure,
};

// Vehicle mode is deliberately not configurable. The implementation always
// installs TWAI in strict listen-only mode and disables its TX queue.
struct Configuration {
  std::uint32_t bitrate_bps{500'000};
  std::uint8_t bus_id{0};
};

struct Statistics {
  std::uint64_t frames_received{0};
  std::uint64_t frames_queued{0};
  std::uint64_t frames_delivered{0};
  std::uint64_t frames_dropped{0};
  std::uint64_t queue_overflows{0};
  std::uint64_t bus_errors{0};
  std::uint64_t driver_rx_missed{0};
  std::uint64_t controller_resets{0};
  std::uint32_t queue_depth{0};
  std::uint32_t queue_high_watermark{0};
  std::uint32_t queue_capacity{kQueueCapacity};
};

enum class StatisticsOperation : std::uint8_t {
  kSnapshot,
  // Reset interval counters after taking the returned snapshot. Queued frames
  // remain intact, queue_depth is unchanged, and the new interval watermark
  // starts at (and cannot fall below) the live queue depth.
  kSnapshotAndReset,
};

Result start(const Configuration &configuration) noexcept;
Result stop() noexcept;
// The receive boundary is SPSC: one acquisition task produces frames and one
// consumer task owns calls to receive(). Callers must serialize receive() calls
// and stop that consumer before restarting acquisition.
Result receive(vehicle_core::RawCanFrame &frame, std::uint32_t timeout_ms) noexcept;
Statistics statistics(StatisticsOperation operation = StatisticsOperation::kSnapshot) noexcept;

} // namespace can_bus
