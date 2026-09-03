#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace vehicle_core {

constexpr std::uint32_t kApiVersion = 1;
constexpr std::size_t kCanClassicPayloadBytes = 8;

using MonotonicTimestamp = std::uint64_t;
using Microseconds = std::uint64_t;

enum class CanIdentifierFormat : std::uint8_t { Standard, Extended };

// A receive-only, classic-CAN frame. The fixed payload keeps the type safe to
// copy through bounded queues and prevents ownership of a buffer from leaking
// through the domain API.
struct RawCanFrame {
  MonotonicTimestamp timestamp_us{0};
  std::uint8_t bus_id{0};
  std::uint32_t identifier{0};
  CanIdentifierFormat identifier_format{CanIdentifierFormat::Standard};
  bool remote_request{false};
  std::uint8_t dlc{0};
  std::array<std::uint8_t, kCanClassicPayloadBytes> data{};

  [[nodiscard]] bool is_valid() const noexcept;
  [[nodiscard]] bool is_extended() const noexcept {
    return identifier_format == CanIdentifierFormat::Extended;
  }
};

enum class SignalStatus : std::uint8_t { Unknown, Valid, Stale };

// Units are identifiers rather than strings, so signal metadata has static
// storage and cannot dangle. Additions must remain transport-independent.
enum class SignalUnit : std::uint8_t {
  None,
  KilometresPerHour,
  RevolutionsPerMinute,
  SelectorPosition,
  ActualGear,
  Boolean,
  TurnState,
};

enum class SelectorPosition : std::uint8_t { Unknown, Park, Reverse, Neutral, Drive };

enum class ActualGear : std::uint8_t {
  Unknown,
  Park,
  Neutral,
  Reverse,
  First,
  Second,
  Third,
  Fourth,
  Fifth,
  Sixth,
};

constexpr Microseconds kTurnFreshnessTimeoutUs = 250'000;
constexpr Microseconds kRequestFreshnessTimeoutUs = 250'000;

// Only turn/request freshness is confirmed by the project requirements. The
// remaining policies are intentionally unset until signal evidence exists.
struct VehicleFreshnessPolicy {
  std::optional<Microseconds> speed_kph_timeout_us{};
  std::optional<Microseconds> engine_rpm_timeout_us{};
  std::optional<Microseconds> selector_position_timeout_us{};
  std::optional<Microseconds> actual_gear_timeout_us{};
  std::optional<Microseconds> turn_state_timeout_us{kTurnFreshnessTimeoutUs};
  std::optional<Microseconds> hazard_request_timeout_us{kRequestFreshnessTimeoutUs};
  std::optional<Microseconds> left_turn_request_timeout_us{kRequestFreshnessTimeoutUs};
  std::optional<Microseconds> right_turn_request_timeout_us{kRequestFreshnessTimeoutUs};
};

template <typename T> struct Signal {
  T value{};
  SignalUnit unit{SignalUnit::None};
  MonotonicTimestamp last_update_us{0};
  std::optional<Microseconds> freshness_timeout_us{};
  SignalStatus status{SignalStatus::Unknown};

  constexpr Signal() noexcept = default;
  constexpr explicit Signal(SignalUnit signal_unit,
                            std::optional<Microseconds> timeout_us = std::nullopt) noexcept
      : unit(signal_unit), freshness_timeout_us(timeout_us) {}

  [[nodiscard]] constexpr bool is_valid() const noexcept { return status == SignalStatus::Valid; }
  [[nodiscard]] constexpr bool is_stale() const noexcept { return status == SignalStatus::Stale; }
  [[nodiscard]] constexpr bool is_unknown() const noexcept {
    return status == SignalStatus::Unknown;
  }

  // Updates are monotonic. A late frame is rejected and cannot revive an old
  // value or move the signal's clock backwards.
  bool update(T new_value, MonotonicTimestamp timestamp_us) noexcept;

  void set_freshness_timeout(std::optional<Microseconds> timeout_us) noexcept {
    freshness_timeout_us = timeout_us;
  }

  // A zero value is still valid after update(); status is never inferred from
  // value. Calling refresh with an elapsed, signal-specific timeout marks only
  // a valid signal stale. Unknown remains unknown until its first update.
  void refresh(MonotonicTimestamp now_us) noexcept;
};

enum class TurnState : std::uint8_t { Unknown, Off, Left, Right, Hazard };

enum class TurnEventType : std::uint8_t { StateChanged };

// Semantic event: intentionally contains no Mazda CAN identifier or raw
// payload. It is safe to pass to a dashboard or effect consumer.
struct TurnEdgeEvent {
  TurnEventType type{TurnEventType::StateChanged};
  TurnState previous{TurnState::Unknown};
  TurnState current{TurnState::Unknown};
  MonotonicTimestamp timestamp_us{0};
};

struct VehicleState {
  MonotonicTimestamp timestamp_us{0};
  Signal<float> speed_kph{SignalUnit::KilometresPerHour};
  Signal<float> engine_rpm{SignalUnit::RevolutionsPerMinute};
  Signal<SelectorPosition> selector_position{SignalUnit::SelectorPosition};
  Signal<ActualGear> actual_gear{SignalUnit::ActualGear};
  Signal<TurnState> turn_state{SignalUnit::TurnState, kTurnFreshnessTimeoutUs};
  Signal<bool> hazard_request{SignalUnit::Boolean, kRequestFreshnessTimeoutUs};
  Signal<bool> left_turn_request{SignalUnit::Boolean, kRequestFreshnessTimeoutUs};
  Signal<bool> right_turn_request{SignalUnit::Boolean, kRequestFreshnessTimeoutUs};

  // Apply a semantic turn state and return an edge only when the state
  // changed. The first known state has Unknown as its previous state.
  std::optional<TurnEdgeEvent> update_turn(TurnState state,
                                           MonotonicTimestamp timestamp_us) noexcept;

  // Unknown and stale turn values are never actionable. Consumers that drive
  // an indicator surface should use this fail-off view rather than the stored
  // value directly.
  [[nodiscard]] TurnState effective_turn_state() const noexcept {
    return turn_state.is_valid() ? turn_state.value : TurnState::Unknown;
  }

  // Return a value snapshot evaluated at now_us. The original state is not
  // mutated, making this suitable for independent consumers.
  [[nodiscard]] VehicleState snapshot(MonotonicTimestamp now_us) const noexcept;
  [[nodiscard]] VehicleState snapshot(MonotonicTimestamp now_us,
                                      const VehicleFreshnessPolicy &policy) const noexcept;

  void refresh(MonotonicTimestamp now_us) noexcept;
  void apply_freshness_policy(const VehicleFreshnessPolicy &policy) noexcept;
};

// A deterministic source of monotonic time. Production code can adapt an
// embedded clock; host tests can use a fixed clock without sleeping.
class MonotonicClock {
public:
  virtual ~MonotonicClock() = default;
  [[nodiscard]] virtual MonotonicTimestamp now() const noexcept = 0;
};

class SnapshotProvider {
public:
  virtual ~SnapshotProvider() = default;
  [[nodiscard]] virtual VehicleState snapshot() const noexcept = 0;
};

// A small state owner for deterministic integrations. It owns one state by
// value, performs no allocation, and exposes snapshots only by value.
class VehicleStateStore final : public SnapshotProvider {
public:
  explicit VehicleStateStore(MonotonicClock &clock, VehicleFreshnessPolicy policy = {}) noexcept;

  [[nodiscard]] VehicleState &mutable_state() noexcept { return state_; }
  [[nodiscard]] const VehicleState &state() const noexcept { return state_; }
  [[nodiscard]] VehicleState snapshot() const noexcept override;

private:
  MonotonicClock *clock_;
  VehicleFreshnessPolicy policy_;
  VehicleState state_{};
};

[[nodiscard]] bool library_is_available() noexcept;

// Candidate Mazda signal definitions and decoders. These are deliberately
// transport- and clock-independent: callers provide a received RawCanFrame
// and the frame's monotonic timestamp. Definitions remain candidates pending
// MCAN-19 validation against the approved vehicle.
namespace mazda_candidate {

constexpr std::uint32_t kEngineDataId = 0x202;
constexpr std::uint32_t kGearId = 0x228;
constexpr std::uint32_t kTurnSwitchId = 0x091;
constexpr std::uint32_t kBlinkInfoId = 0x09a;
constexpr std::uint8_t kCandidateDlc = 8;

enum class DecodeStatus : std::uint8_t { Ignored, Updated, Invalid };

struct CandidateMessageDefinition {
  const char *name;
  std::uint32_t identifier;
  std::uint8_t expected_dlc;
  std::optional<Microseconds> expected_period_us;
  std::optional<Microseconds> freshness_timeout_us;
  bool pending_validation;
  const char *provenance;
};

struct CandidateSignalDefinition {
  const char *name;
  std::uint32_t identifier;
  // Payload bit offset in the decoder's byte/LSB numbering, not DBC Motorola
  // start-bit notation. Numeric big-endian fields are documented by byte
  // offset in the MCAN-14 note.
  std::uint8_t start_bit;
  std::uint8_t bit_length;
  float scale;
  float offset;
  SignalUnit unit;
  float physical_min;
  float physical_max;
  const char *invalid_values;
};

// The opendbc source has no cycle-time declaration. Null periods/timeouts are
// intentional until reviewed replay or isolated-bench evidence establishes
// them; callers can supply per-signal timeouts through VehicleFreshnessPolicy.
inline constexpr CandidateMessageDefinition kEngineDataDefinition{
    "ENGINE_DATA",
    kEngineDataId,
    kCandidateDlc,
    std::nullopt,
    std::nullopt,
    true,
    "comma.ai/opendbc mazda_2017.dbc @ 95f3d52f; candidate pending MCAN-19"};
inline constexpr CandidateMessageDefinition kGearDefinition{
    "GEAR",
    kGearId,
    kCandidateDlc,
    std::nullopt,
    std::nullopt,
    true,
    "comma.ai/opendbc mazda_2017.dbc @ 95f3d52f; candidate pending MCAN-19"};
inline constexpr CandidateMessageDefinition kTurnSwitchDefinition{
    "TURN_SWITCH",
    kTurnSwitchId,
    kCandidateDlc,
    std::nullopt,
    kTurnFreshnessTimeoutUs,
    true,
    "comma.ai/opendbc mazda_2017.dbc @ 95f3d52f; candidate pending MCAN-19"};
// BLINK_INFO is intentionally exposed as provenance only. It must not drive
// request or turn-state updates until a later validation change establishes
// its semantics for the approved vehicle.
inline constexpr CandidateMessageDefinition kBlinkInfoDefinition{
    "BLINK_INFO",
    kBlinkInfoId,
    kCandidateDlc,
    std::nullopt,
    std::nullopt,
    true,
    "comma.ai/opendbc mazda_2017.dbc @ 95f3d52f; diagnostic-only pending MCAN-19"};

inline constexpr CandidateSignalDefinition kEngineRpmDefinition{
    "RPM",
    kEngineDataId,
    0,
    16,
    0.25F,
    0.0F,
    SignalUnit::RevolutionsPerMinute,
    0.0F,
    8500.0F,
    "raw values above 34000 (8500 rpm)"};
inline constexpr CandidateSignalDefinition kEngineSpeedDefinition{
    "SPEED",
    kEngineDataId,
    16,
    16,
    0.01F,
    0.0F,
    SignalUnit::KilometresPerHour,
    0.0F,
    32767.0F,
    "none declared by source; representable values are bounded by 16 bits"};
inline constexpr CandidateSignalDefinition kSelectorDefinition{"GEAR",
                                                               kGearId,
                                                               0,
                                                               3,
                                                               1.0F,
                                                               0.0F,
                                                               SignalUnit::SelectorPosition,
                                                               0.0F,
                                                               4.0F,
                                                               "0=shifting/unknown; 5-7=undefined"};
inline constexpr CandidateSignalDefinition kActualGearDefinition{
    "GEAR_BOX",
    kGearId,
    33,
    4,
    1.0F,
    0.0F,
    SignalUnit::ActualGear,
    0.0F,
    14.0F,
    "7-13=undefined; 15=shifting/unknown; 14=reverse"};
inline constexpr CandidateSignalDefinition kHazardDefinition{
    "HAZARD", kTurnSwitchId,       10,   1,    1.0F,
    0.0F,     SignalUnit::Boolean, 0.0F, 1.0F, "none declared by source; candidate bit"};
inline constexpr CandidateSignalDefinition kTurnRightSwitchDefinition{
    "TURN_RIGHT_SWITCH",
    kTurnSwitchId,
    12,
    1,
    1.0F,
    0.0F,
    SignalUnit::Boolean,
    0.0F,
    1.0F,
    "none declared by source; candidate bit"};
inline constexpr CandidateSignalDefinition kTurnLeftSwitchDefinition{
    "TURN_LEFT_SWITCH",
    kTurnSwitchId,
    13,
    1,
    1.0F,
    0.0F,
    SignalUnit::Boolean,
    0.0F,
    1.0F,
    "none declared by source; candidate bit"};

[[nodiscard]] DecodeStatus decode_engine_data(const RawCanFrame &frame,
                                              VehicleState &state) noexcept;
[[nodiscard]] DecodeStatus decode_gear(const RawCanFrame &frame, VehicleState &state) noexcept;
[[nodiscard]] DecodeStatus
decode_turn_switch(const RawCanFrame &frame, VehicleState &state,
                   std::optional<TurnEdgeEvent> *edge = nullptr) noexcept;
// Dispatches only validated candidate messages. BLINK_INFO is deliberately
// ignored so diagnostic phase bits cannot alter actionable state.
[[nodiscard]] DecodeStatus decode(const RawCanFrame &frame, VehicleState &state,
                                  std::optional<TurnEdgeEvent> *edge = nullptr) noexcept;

} // namespace mazda_candidate

} // namespace vehicle_core

// Template definitions stay in the public header so the portable API works
// for application-defined signal value types without a runtime registry.
namespace vehicle_core {

template <typename T> bool Signal<T>::update(T new_value, MonotonicTimestamp timestamp) noexcept {
  if (status != SignalStatus::Unknown && timestamp < last_update_us) {
    return false;
  }
  value = new_value;
  last_update_us = timestamp;
  status = SignalStatus::Valid;
  return true;
}

template <typename T> void Signal<T>::refresh(MonotonicTimestamp now) noexcept {
  if (status != SignalStatus::Valid || now < last_update_us) {
    return;
  }
  if (!freshness_timeout_us.has_value()) {
    if (now > last_update_us) {
      status = SignalStatus::Stale;
    }
    return;
  }
  if ((now - last_update_us) > *freshness_timeout_us) {
    status = SignalStatus::Stale;
  }
}

} // namespace vehicle_core
