#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "vehicle_core/vehicle_core.hpp"

namespace raw_capture {

inline constexpr std::size_t kFrameQueueCapacity = 64;
inline constexpr std::uint64_t kDefaultDiagnosticIntervalUs = 1'000'000;
inline constexpr std::uint64_t kDefaultStatisticsIntervalUs = 1'000'000;
inline constexpr std::size_t kDropBoundaryCapacity = 256;

// The exporter only asks for a copy. A source implementation must not expose
// queue storage and must return immediately when no frame is available.
class FrameSource {
public:
  virtual ~FrameSource() = default;
  [[nodiscard]] virtual bool try_receive(vehicle_core::RawCanFrame &frame) noexcept = 0;
};

enum class WriteResult : std::uint8_t {
  kWritten,
  kWouldBlock,
  kDisconnected,
  kError,
};

// Output is deliberately a write-only boundary. There is no read, command,
// acknowledgement, or CAN operation in this interface.
class OutputSink {
public:
  virtual ~OutputSink() = default;
  [[nodiscard]] virtual bool connected() const noexcept = 0;
  [[nodiscard]] virtual WriteResult write(std::string_view complete_line) noexcept = 0;
};

struct SessionMetadata {
  std::string_view firmware;
  std::string_view board;
  std::uint32_t bitrate_bps{500'000};
  std::uint64_t clock_hz{1'000'000};
  std::uint64_t dropped_frames{0};
  std::uint64_t dropped_records{0};
};

struct Configuration {
  SessionMetadata session{};
  std::uint64_t diagnostic_interval_us{kDefaultDiagnosticIntervalUs};
  std::uint64_t statistics_interval_us{kDefaultStatisticsIntervalUs};
};

struct Statistics {
  std::uint64_t input_frames{0};
  std::uint64_t queued_frames{0};
  std::uint64_t emitted_frames{0};
  std::uint64_t dropped_frames{0};
  std::uint64_t dropped_records{0};
  std::uint64_t queue_overflows{0};
  std::uint64_t emitted_diagnostics{0};
  std::uint32_t queue_depth{0};
  std::uint32_t queue_high_watermark{0};
};

// A bounded, two-stage exporter. One owner task must call poll_input() then
// poll_output() in that order; the methods are intentionally not concurrent.
// The input phase copies frames into private storage and never waits for
// output, while the output phase can be driven at a lower effective rate.
class Exporter final {
public:
  explicit Exporter(Configuration configuration) noexcept;

  // Drain at most max_frames source copies. A full exporter queue uses
  // drop-newest and creates an observable DROP record.
  std::size_t poll_input(FrameSource &source, std::size_t max_frames) noexcept;

  // Write at most max_records complete lines. A disconnected or slow sink
  // leaves queued frames intact and returns without blocking the source task.
  std::size_t poll_output(OutputSink &sink, std::uint64_t now_us,
                          std::size_t max_records = 1) noexcept;

  // Feed a loss observed by an upstream boundary (for example the CAN ring)
  // into the same cumulative counters and DROP diagnostic path.
  void note_dropped_frames(std::uint64_t count, std::uint64_t timestamp_us,
                           std::uint8_t bus_id = 0xff,
                           std::string_view reason = "upstream-overflow") noexcept;

  // Request the final checkpoint required before an orderly shutdown.
  void request_final_statistics() noexcept { final_statistics_pending_ = true; }

  [[nodiscard]] Statistics statistics() const noexcept;
  [[nodiscard]] bool failed() const noexcept { return failed_; }

private:
  struct Queue {
    struct Item {
      vehicle_core::RawCanFrame frame{};
      std::uint64_t segment{0};
    };
    std::array<Item, kFrameQueueCapacity> frames{};
    std::size_t head{0};
    std::size_t tail{0};
    std::uint32_t high_watermark{0};

    [[nodiscard]] bool push(const vehicle_core::RawCanFrame &frame) noexcept;
    [[nodiscard]] bool pop(vehicle_core::RawCanFrame &frame) noexcept;
    [[nodiscard]] std::uint32_t depth() const noexcept;
  };

  struct DropBoundary {
    std::uint64_t count{0};
    std::uint64_t timestamp_us{0};
    std::uint64_t segment{0};
    std::uint8_t bus{0xff};
    std::array<char, 64> reason{};
    std::size_t reason_size{0};
  };

  enum class Attempt : std::uint8_t { kWritten, kDeferred, kFailed };

  [[nodiscard]] Attempt write_line(OutputSink &sink, std::string_view line) noexcept;
  [[nodiscard]] Attempt write_header(OutputSink &sink) noexcept;
  [[nodiscard]] Attempt write_session(OutputSink &sink) noexcept;
  [[nodiscard]] Attempt write_discontinuity(OutputSink &sink, std::uint64_t now_us) noexcept;
  [[nodiscard]] Attempt write_drop(OutputSink &sink) noexcept;
  [[nodiscard]] Attempt write_statistics(OutputSink &sink, std::uint64_t now_us) noexcept;
  [[nodiscard]] Attempt write_frame(OutputSink &sink) noexcept;
  [[nodiscard]] bool diagnostic_allowed(std::uint64_t now_us) const noexcept;
  void record_drop(std::uint64_t count, std::uint64_t timestamp_us, std::uint8_t bus_id,
                   std::string_view reason) noexcept;
  void schedule_next_diagnostic(std::uint64_t now_us) noexcept;
  [[nodiscard]] static bool valid_utf8(std::string_view value) noexcept;
  [[nodiscard]] static bool copy_text(std::array<char, 128> &destination, std::size_t &size,
                                      std::string_view value) noexcept;

  Configuration configuration_;
  std::array<char, 128> firmware_{};
  std::array<char, 128> board_{};
  std::array<char, 128> pending_drop_reason_storage_{};
  std::size_t firmware_size_{0};
  std::size_t board_size_{0};
  std::size_t pending_drop_reason_size_{0};
  Queue queue_{};
  std::uint64_t input_frames_{0};
  std::uint64_t queued_frames_{0};
  std::uint64_t emitted_frames_{0};
  std::uint64_t dropped_frames_{0};
  std::uint64_t dropped_records_{0};
  std::uint64_t queue_overflows_{0};
  std::uint64_t emitted_diagnostics_{0};
  std::uint64_t pending_drop_frames_{0};
  std::uint64_t pending_drop_timestamp_us_{0};
  std::uint64_t pending_drop_segment_{0};
  std::array<DropBoundary, kDropBoundaryCapacity> drop_boundaries_{};
  std::size_t drop_head_{0};
  std::size_t drop_tail_{0};
  std::uint8_t pending_drop_bus_{0xff};
  std::uint64_t segment_{0};
  std::uint64_t capture_segment_{0};
  std::uint64_t next_diagnostic_us_{0};
  std::uint64_t next_statistics_us_{0};
  bool header_emitted_{false};
  bool session_emitted_{false};
  bool connection_latched_{false};
  bool reconnect_pending_{false};
  bool final_statistics_pending_{false};
  bool failed_{false};
};

} // namespace raw_capture
