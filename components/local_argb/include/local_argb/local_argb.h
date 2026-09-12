#pragma once

#include <cstdint>

#include "vehicle_core/time.hpp"

// Compatibility declarations for the Stage 1 semantic adapter.  The
// renderer itself does not include Mazda headers; Stage 3 will remove these
// declarations when the application wiring moves to vehicle_lighting_policy.
namespace mazda {
enum class TurnState : std::uint8_t;
struct VehicleState;
} // namespace mazda

namespace vehicle_core {
enum class SignalStatus : std::uint8_t;
} // namespace vehicle_core

namespace local_argb {

inline constexpr std::uint8_t kBrightnessCeiling = 16;
inline constexpr vehicle_core::Microseconds kFailOffTimeoutUs = 250'000;
inline constexpr vehicle_core::Microseconds kPublishHeartbeatUs = 100'000;
inline constexpr vehicle_core::Microseconds kDriverHangRestartUs = 100'000;
inline constexpr vehicle_core::Microseconds kWorkerStallRestartUs = 100'000;
inline constexpr vehicle_core::Microseconds kSupervisorPollUs = 10'000;
inline constexpr vehicle_core::Microseconds kDriverRestartRequestBoundUs =
    kDriverHangRestartUs + kSupervisorPollUs;
inline constexpr vehicle_core::Microseconds kWorkerRestartRequestBoundUs =
    kWorkerStallRestartUs + kSupervisorPollUs;

struct Rgb {
  std::uint8_t red{0};
  std::uint8_t green{0};
  std::uint8_t blue{0};
};

constexpr bool operator==(const Rgb &left, const Rgb &right) noexcept {
  return left.red == right.red && left.green == right.green && left.blue == right.blue;
}
constexpr bool operator!=(const Rgb &left, const Rgb &right) noexcept { return !(left == right); }

// Generic renderer command. Stage 2 policy commands are adapted to the
// implementation-only sink record before queue submission.
struct LightingCommand {
  Rgb color{};
  vehicle_core::MonotonicTimestamp valid_until_us{0};
  bool actionable{false};
};

inline constexpr Rgb kBlack{};
inline constexpr Rgb kLeftGreen{0, kBrightnessCeiling, 0};
inline constexpr Rgb kRightBlue{0, 0, kBrightnessCeiling};
inline constexpr Rgb kHazardAmber{kBrightnessCeiling, kBrightnessCeiling / 2, 0};

enum class SemanticHealth : std::uint8_t { Online, CanOffline, DecoderError };

struct SemanticSnapshot {
  mazda::TurnState turn{};
  vehicle_core::SignalStatus turn_status{};
  vehicle_core::MonotonicTimestamp turn_last_update_us{0};
  SemanticHealth health{SemanticHealth::CanOffline};
};

[[nodiscard]] SemanticSnapshot from_vehicle_state(const mazda::VehicleState &state,
                                                  SemanticHealth health) noexcept;
[[nodiscard]] Rgb color_for(const SemanticSnapshot &snapshot,
                            vehicle_core::MonotonicTimestamp now_us) noexcept;

class PixelSink {
public:
  virtual ~PixelSink() = default;
  virtual bool write(Rgb color) noexcept = 0;
};

// Deterministic single-pixel state machine used by both host tests and the
// lower-priority ESP-IDF worker. Only copied semantic state crosses this API.
class Controller {
public:
  explicit Controller(PixelSink &sink) noexcept : sink_(&sink) {}

  bool start() noexcept;
  // Generic renderer entry point. The command's absolute deadline is copied
  // unchanged; renderer heartbeats therefore cannot extend sample validity.
  bool apply(LightingCommand command, vehicle_core::MonotonicTimestamp now_us) noexcept;
  // Stage 1 compatibility adapter. New consumers should use the typed policy
  // and generic command overload above.
  bool apply(SemanticSnapshot snapshot, vehicle_core::MonotonicTimestamp now_us) noexcept;
  bool tick(vehicle_core::MonotonicTimestamp now_us) noexcept;
  [[nodiscard]] bool faulted() const noexcept { return faulted_; }

private:
  bool write_desired(Rgb desired) noexcept;

  PixelSink *sink_;
  LightingCommand command_{};
  bool has_command_{false};
  SemanticSnapshot snapshot_{};
  Rgb last_written_{};
  bool has_snapshot_{false};
  bool has_last_written_{false};
  bool faulted_{false};
};

// Portable model of the embedded length-one overwrite queue.
class Mailbox {
public:
  void submit(LightingCommand command) noexcept {
    command_ = command;
    has_command_ = true;
  }
  [[nodiscard]] bool take(LightingCommand &command) noexcept {
    if (!has_command_) {
      return false;
    }
    command = command_;
    has_command_ = false;
    return true;
  }

  // Stage 1 compatibility adapter; policy/service code uses LightingCommand.
  void submit(SemanticSnapshot snapshot) noexcept {
    pending_ = snapshot;
    has_pending_ = true;
  }
  [[nodiscard]] bool take(SemanticSnapshot &snapshot) noexcept {
    if (!has_pending_) {
      return false;
    }
    snapshot = pending_;
    has_pending_ = false;
    return true;
  }

private:
  LightingCommand command_{};
  bool has_command_{false};
  SemanticSnapshot pending_{};
  bool has_pending_{false};
};

// Limits producer-to-worker wakeups to visible semantic changes and a bounded
// health heartbeat. The heartbeat carries the latest freshness timestamp
// without waking the worker for every repeated turn frame.
class PublicationPolicy {
public:
  [[nodiscard]] bool should_publish(SemanticSnapshot snapshot,
                                    vehicle_core::MonotonicTimestamp now_us) noexcept;

private:
  SemanticSnapshot last_published_{};
  vehicle_core::MonotonicTimestamp last_publish_us_{0};
  bool has_published_{false};
};

// Portable model for the supervisor around the third-party blocking refresh
// call. Production synchronizes access to this model between two tasks.
class DriverWatchdog {
public:
  void begin(vehicle_core::MonotonicTimestamp now_us) noexcept {
    started_us_ = now_us;
    in_progress_ = true;
  }
  void end() noexcept { in_progress_ = false; }
  [[nodiscard]] bool restart_due(vehicle_core::MonotonicTimestamp now_us) const noexcept {
    return in_progress_ && (now_us < started_us_ || now_us - started_us_ > kDriverHangRestartUs);
  }

private:
  vehicle_core::MonotonicTimestamp started_us_{0};
  bool in_progress_{false};
};

// Portable progress lease for the complete worker loop, including code before
// and after the blocking driver call. Production protects it with the same
// supervisor lock as DriverWatchdog.
class WorkerLease {
public:
  void arm(vehicle_core::MonotonicTimestamp now_us) noexcept {
    last_progress_us_ = now_us;
    armed_ = true;
  }
  void heartbeat(vehicle_core::MonotonicTimestamp now_us) noexcept {
    if (armed_) {
      last_progress_us_ = now_us;
    }
  }
  void disarm() noexcept { armed_ = false; }
  [[nodiscard]] bool restart_due(vehicle_core::MonotonicTimestamp now_us) const noexcept {
    return armed_ &&
           (now_us < last_progress_us_ || now_us - last_progress_us_ > kWorkerStallRestartUs);
  }

private:
  vehicle_core::MonotonicTimestamp last_progress_us_{0};
  bool armed_{false};
};

// ESP-IDF runtime. start() sends an explicit black RMT frame before returning.
bool start() noexcept;
bool submit(LightingCommand command) noexcept;
// Stage 1 compatibility adapter. The generic overload is the private sink's
// value boundary used by the Stage 2 service.
bool submit(SemanticSnapshot snapshot) noexcept;
void fail_off() noexcept;

} // namespace local_argb
