#pragma once

#include <cstdint>

#include "vehicle_core/vehicle_core.hpp"

namespace local_argb {

inline constexpr std::uint8_t kBrightnessCeiling = 16;
inline constexpr vehicle_core::Microseconds kFailOffTimeoutUs = 250'000;

struct Rgb {
  std::uint8_t red{0};
  std::uint8_t green{0};
  std::uint8_t blue{0};
};

constexpr bool operator==(const Rgb &left, const Rgb &right) noexcept {
  return left.red == right.red && left.green == right.green && left.blue == right.blue;
}
constexpr bool operator!=(const Rgb &left, const Rgb &right) noexcept { return !(left == right); }

inline constexpr Rgb kBlack{};
inline constexpr Rgb kLeftGreen{0, kBrightnessCeiling, 0};
inline constexpr Rgb kRightBlue{0, 0, kBrightnessCeiling};
inline constexpr Rgb kHazardAmber{kBrightnessCeiling, kBrightnessCeiling / 2, 0};

enum class SemanticHealth : std::uint8_t { Online, CanOffline, DecoderError };

struct SemanticSnapshot {
  vehicle_core::TurnState turn{vehicle_core::TurnState::Unknown};
  vehicle_core::SignalStatus turn_status{vehicle_core::SignalStatus::Unknown};
  vehicle_core::MonotonicTimestamp turn_last_update_us{0};
  SemanticHealth health{SemanticHealth::CanOffline};
};

[[nodiscard]] SemanticSnapshot from_vehicle_state(const vehicle_core::VehicleState &state,
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
  bool apply(SemanticSnapshot snapshot, vehicle_core::MonotonicTimestamp now_us) noexcept;
  bool tick(vehicle_core::MonotonicTimestamp now_us) noexcept;
  [[nodiscard]] bool faulted() const noexcept { return faulted_; }

private:
  bool write_desired(Rgb desired) noexcept;

  PixelSink *sink_;
  SemanticSnapshot snapshot_{};
  Rgb last_written_{};
  bool has_snapshot_{false};
  bool has_last_written_{false};
  bool faulted_{false};
};

// Portable model of the embedded length-one overwrite queue.
class Mailbox {
public:
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
  SemanticSnapshot pending_{};
  bool has_pending_{false};
};

// ESP-IDF runtime. start() sends an explicit black RMT frame before returning.
bool start() noexcept;
bool submit(SemanticSnapshot snapshot) noexcept;
void fail_off() noexcept;

} // namespace local_argb
