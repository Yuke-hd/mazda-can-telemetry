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
  WiperPosition,
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
  // The DBC's raw zero value is labelled P_or_N. Keep the existing Park
  // representation source-compatible while exposing the source label.
  ParkOrNeutral = Park,
};

enum class FrontWiperPosition : std::uint8_t {
  Unknown,
  Off,
  On,
  High,
  Intermittent,
};

constexpr Microseconds kTurnFreshnessTimeoutUs = 250'000;
constexpr Microseconds kRequestFreshnessTimeoutUs = 250'000;

// Only turn/request freshness has a confirmed default. The supplied DBC has
// no cycle-time declarations, so the remaining policies stay unconfigured.
struct VehicleFreshnessPolicy {
  std::optional<Microseconds> speed_kph_timeout_us{};
  std::optional<Microseconds> engine_rpm_timeout_us{};
  std::optional<Microseconds> selector_position_timeout_us{};
  std::optional<Microseconds> actual_gear_timeout_us{};
  std::optional<Microseconds> turn_state_timeout_us{kTurnFreshnessTimeoutUs};
  std::optional<Microseconds> hazard_request_timeout_us{kRequestFreshnessTimeoutUs};
  std::optional<Microseconds> left_turn_request_timeout_us{kRequestFreshnessTimeoutUs};
  std::optional<Microseconds> right_turn_request_timeout_us{kRequestFreshnessTimeoutUs};
  std::optional<Microseconds> liftgate_open_timeout_us{};
  std::optional<Microseconds> rear_right_door_open_timeout_us{};
  std::optional<Microseconds> rear_left_door_open_timeout_us{};
  std::optional<Microseconds> front_left_door_open_rhd_timeout_us{};
  std::optional<Microseconds> front_right_door_open_rhd_timeout_us{};
  std::optional<Microseconds> doors_unlocked_timeout_us{};
  std::optional<Microseconds> left_indicator_lamp_timeout_us{};
  std::optional<Microseconds> right_indicator_lamp_timeout_us{};
  std::optional<Microseconds> wiper_low_timeout_us{};
  std::optional<Microseconds> front_wiper_timeout_us{};
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
  Signal<bool> liftgate_open{SignalUnit::Boolean};
  Signal<bool> rear_right_door_open{SignalUnit::Boolean};
  Signal<bool> rear_left_door_open{SignalUnit::Boolean};
  Signal<bool> front_left_door_open_rhd{SignalUnit::Boolean};
  Signal<bool> front_right_door_open_rhd{SignalUnit::Boolean};
  Signal<bool> doors_unlocked{SignalUnit::Boolean};
  Signal<bool> left_indicator_lamp{SignalUnit::Boolean};
  Signal<bool> right_indicator_lamp{SignalUnit::Boolean};
  Signal<bool> wiper_low{SignalUnit::Boolean};
  Signal<FrontWiperPosition> front_wiper{SignalUnit::WiperPosition};

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

// Mazda signal definitions and decoders. These are deliberately transport- and
// clock-independent: callers provide a received RawCanFrame and the frame's
// monotonic timestamp. Confirmed definitions are capture-derived; the existing
// speed definition remains a candidate until separately validated.
namespace mazda_candidate {

constexpr std::uint32_t kEngineDataId = 0x202;
constexpr std::uint32_t kTransmissionId = 0x228;
// Retain the original symbol for callers that used the candidate name.
constexpr std::uint32_t kGearId = kTransmissionId;
constexpr std::uint32_t kDoorsId = 0x43e;
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
  // The reviewed DBC start bit and byte order are retained separately so the
  // decoder-facing coordinates above remain source-compatible.
  std::uint8_t dbc_start_bit{0};
  enum class ByteOrder : std::uint8_t { Motorola, Intel };
  ByteOrder byte_order{ByteOrder::Motorola};
  const char *value_table{nullptr};
  const char *provenance{nullptr};
};

inline constexpr const char *kCaptureConfirmedProvenance =
    "capture-derived Mazda custom DBC; confirmed from passive real-world capture";

// The capture-derived DBC has no cycle-time declaration. Null periods/timeouts
// are intentional until reviewed replay or isolated-bench evidence establishes
// them; callers can supply per-signal timeouts through VehicleFreshnessPolicy.
inline constexpr CandidateMessageDefinition kEngineDataDefinition{"ENGINE_DATA",
                                                                  kEngineDataId,
                                                                  kCandidateDlc,
                                                                  std::nullopt,
                                                                  std::nullopt,
                                                                  false,
                                                                  kCaptureConfirmedProvenance};
inline constexpr CandidateMessageDefinition kGearDefinition{"TRANSMISSION",
                                                            kTransmissionId,
                                                            kCandidateDlc,
                                                            std::nullopt,
                                                            std::nullopt,
                                                            false,
                                                            kCaptureConfirmedProvenance};
// Keep the earlier public name while exposing the DBC message name above.
inline constexpr const CandidateMessageDefinition &kTransmissionDefinition = kGearDefinition;
inline constexpr CandidateMessageDefinition kDoorsDefinition{"DOORS",
                                                             kDoorsId,
                                                             kCandidateDlc,
                                                             std::nullopt,
                                                             std::nullopt,
                                                             false,
                                                             kCaptureConfirmedProvenance};
inline constexpr CandidateMessageDefinition kTurnSwitchDefinition{"TURN_SWITCH",
                                                                  kTurnSwitchId,
                                                                  kCandidateDlc,
                                                                  std::nullopt,
                                                                  kTurnFreshnessTimeoutUs,
                                                                  false,
                                                                  kCaptureConfirmedProvenance};
inline constexpr CandidateMessageDefinition kBlinkInfoDefinition{"BLINK_INFO",
                                                                 kBlinkInfoId,
                                                                 kCandidateDlc,
                                                                 std::nullopt,
                                                                 std::nullopt,
                                                                 false,
                                                                 kCaptureConfirmedProvenance};

inline constexpr CandidateSignalDefinition kEngineRpmDefinition{
    "EngineRPM",
    kEngineDataId,
    0,
    16,
    0.25F,
    0.0F,
    SignalUnit::RevolutionsPerMinute,
    0.0F,
    8500.0F,
    "raw values above 34000 (8500 rpm)",
    7,
    CandidateSignalDefinition::ByteOrder::Motorola,
    "none",
    kCaptureConfirmedProvenance};
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
inline constexpr CandidateSignalDefinition kSelectorDefinition{
    "Selector",
    kGearId,
    0,
    3,
    1.0F,
    0.0F,
    SignalUnit::SelectorPosition,
    0.0F,
    7.0F,
    "0=Shifting; 5=Unknown_5; 6=Unknown_6; 7=Unknown_7",
    2,
    CandidateSignalDefinition::ByteOrder::Motorola,
    "0=Shifting; 1=Park; 2=Reverse; 3=Neutral; 4=Drive; 5=Unknown_5; 6=Unknown_6; 7=Unknown_7",
    kCaptureConfirmedProvenance};
inline constexpr CandidateSignalDefinition kActualGearDefinition{
    "ActualGear",
    kGearId,
    33,
    4,
    1.0F,
    0.0F,
    SignalUnit::ActualGear,
    0.0F,
    15.0F,
    "7=Unknown_7; 8=Unknown_8; 9=Unknown_9; 10=Unknown_10; 11=Unknown_11; 12=Unknown_12; "
    "13=Unknown_13; 15=Shifting",
    36,
    CandidateSignalDefinition::ByteOrder::Motorola,
    "0=P_or_N; 1=1st; 2=2nd; 3=3rd; 4=4th; 5=5th; 6=6th; 7=Unknown_7; 8=Unknown_8; 9=Unknown_9; "
    "10=Unknown_10; 11=Unknown_11; 12=Unknown_12; 13=Unknown_13; 14=Reverse; 15=Shifting",
    kCaptureConfirmedProvenance};
inline constexpr CandidateSignalDefinition kLiftgateOpenDefinition{
    "Liftgate_Open",
    kDoorsId,
    32,
    1,
    1.0F,
    0.0F,
    SignalUnit::Boolean,
    0.0F,
    1.0F,
    "none",
    32,
    CandidateSignalDefinition::ByteOrder::Motorola,
    "0=Closed; 1=Open",
    kCaptureConfirmedProvenance};
inline constexpr CandidateSignalDefinition kRearRightDoorOpenDefinition{
    "RearRightDoor_Open",
    kDoorsId,
    34,
    1,
    1.0F,
    0.0F,
    SignalUnit::Boolean,
    0.0F,
    1.0F,
    "none",
    34,
    CandidateSignalDefinition::ByteOrder::Motorola,
    "0=Closed; 1=Open",
    kCaptureConfirmedProvenance};
inline constexpr CandidateSignalDefinition kRearLeftDoorOpenDefinition{
    "RearLeftDoor_Open",
    kDoorsId,
    35,
    1,
    1.0F,
    0.0F,
    SignalUnit::Boolean,
    0.0F,
    1.0F,
    "none",
    35,
    CandidateSignalDefinition::ByteOrder::Motorola,
    "0=Closed; 1=Open",
    kCaptureConfirmedProvenance};
inline constexpr CandidateSignalDefinition kFrontLeftDoorOpenRhdDefinition{
    "FrontLeftDoor_Open_RHD",
    kDoorsId,
    36,
    1,
    1.0F,
    0.0F,
    SignalUnit::Boolean,
    0.0F,
    1.0F,
    "none",
    36,
    CandidateSignalDefinition::ByteOrder::Motorola,
    "0=Closed; 1=Open",
    kCaptureConfirmedProvenance};
inline constexpr const CandidateSignalDefinition &kFrontLeftDoorOpenDefinition =
    kFrontLeftDoorOpenRhdDefinition;
inline constexpr CandidateSignalDefinition kFrontRightDoorOpenRhdDefinition{
    "FrontRightDoor_Open_RHD",
    kDoorsId,
    37,
    1,
    1.0F,
    0.0F,
    SignalUnit::Boolean,
    0.0F,
    1.0F,
    "none",
    37,
    CandidateSignalDefinition::ByteOrder::Motorola,
    "0=Closed; 1=Open",
    kCaptureConfirmedProvenance};
inline constexpr CandidateSignalDefinition kDoorsUnlockedDefinition{
    "DoorsUnlocked",
    kDoorsId,
    30,
    1,
    1.0F,
    0.0F,
    SignalUnit::Boolean,
    0.0F,
    1.0F,
    "none",
    30,
    CandidateSignalDefinition::ByteOrder::Motorola,
    "0=Locked; 1=Unlocked",
    kCaptureConfirmedProvenance};
inline constexpr CandidateSignalDefinition kLeftIndicatorLampDefinition{
    "LeftIndicatorLamp",
    kBlinkInfoId,
    18,
    1,
    1.0F,
    0.0F,
    SignalUnit::Boolean,
    0.0F,
    1.0F,
    "none",
    18,
    CandidateSignalDefinition::ByteOrder::Intel,
    "0=Off; 1=On",
    kCaptureConfirmedProvenance};
inline constexpr CandidateSignalDefinition kRightIndicatorLampDefinition{
    "RightIndicatorLamp",
    kBlinkInfoId,
    19,
    1,
    1.0F,
    0.0F,
    SignalUnit::Boolean,
    0.0F,
    1.0F,
    "none",
    19,
    CandidateSignalDefinition::ByteOrder::Motorola,
    "0=Off; 1=On",
    kCaptureConfirmedProvenance};
inline constexpr CandidateSignalDefinition kWiperLowDefinition{
    "WiperLow",
    kBlinkInfoId,
    33,
    1,
    1.0F,
    0.0F,
    SignalUnit::Boolean,
    0.0F,
    1.0F,
    "none",
    33,
    CandidateSignalDefinition::ByteOrder::Motorola,
    "0=Off; 1=On",
    kCaptureConfirmedProvenance};
inline constexpr CandidateSignalDefinition kHazardDefinition{
    "HazardSwitch",
    kTurnSwitchId,
    10,
    1,
    1.0F,
    0.0F,
    SignalUnit::Boolean,
    0.0F,
    1.0F,
    "none",
    10,
    CandidateSignalDefinition::ByteOrder::Motorola,
    "0=Off; 1=On",
    kCaptureConfirmedProvenance};
inline constexpr CandidateSignalDefinition kTurnRightSwitchDefinition{
    "RightIndicatorSwitch",
    kTurnSwitchId,
    12,
    1,
    1.0F,
    0.0F,
    SignalUnit::Boolean,
    0.0F,
    1.0F,
    "none",
    12,
    CandidateSignalDefinition::ByteOrder::Motorola,
    "0=Off; 1=On",
    kCaptureConfirmedProvenance};
inline constexpr CandidateSignalDefinition kTurnLeftSwitchDefinition{
    "LeftIndicatorSwitch",
    kTurnSwitchId,
    13,
    1,
    1.0F,
    0.0F,
    SignalUnit::Boolean,
    0.0F,
    1.0F,
    "none",
    13,
    CandidateSignalDefinition::ByteOrder::Motorola,
    "0=Off; 1=On",
    kCaptureConfirmedProvenance};
inline constexpr CandidateSignalDefinition kFrontWiperDefinition{
    "FrontWiper",
    kTurnSwitchId,
    20,
    2,
    1.0F,
    0.0F,
    SignalUnit::WiperPosition,
    0.0F,
    3.0F,
    "none",
    21,
    CandidateSignalDefinition::ByteOrder::Motorola,
    "0=Off; 1=On; 2=High; 3=Intermittent",
    kCaptureConfirmedProvenance};

[[nodiscard]] DecodeStatus decode_engine_data(const RawCanFrame &frame,
                                              VehicleState &state) noexcept;
[[nodiscard]] DecodeStatus decode_gear(const RawCanFrame &frame, VehicleState &state) noexcept;
[[nodiscard]] DecodeStatus decode_doors(const RawCanFrame &frame, VehicleState &state) noexcept;
[[nodiscard]] DecodeStatus decode_blink_info(const RawCanFrame &frame,
                                             VehicleState &state) noexcept;
[[nodiscard]] DecodeStatus
decode_turn_switch(const RawCanFrame &frame, VehicleState &state,
                   std::optional<TurnEdgeEvent> *edge = nullptr) noexcept;
// Dispatches only validated, capture-confirmed messages plus the existing
// speed candidate carried by ENGINE_DATA.
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
