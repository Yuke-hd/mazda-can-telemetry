#pragma once

#include <cstdint>
#include <optional>

#include "mazda/freshness.hpp"
#include "vehicle_core/frame.hpp"
#include "vehicle_core/signal.hpp"

namespace mazda::candidate {

constexpr std::uint32_t kEngineDataId = 0x202;
constexpr std::uint32_t kTransmissionId = 0x228;
// Retain the original symbol for callers that used the candidate name.
constexpr std::uint32_t kGearId = kTransmissionId;
constexpr std::uint32_t kDoorsId = 0x43e;
constexpr std::uint32_t kTurnSwitchId = 0x091;
constexpr std::uint32_t kBlinkInfoId = 0x09a;
constexpr std::uint8_t kCandidateDlc = 8;

struct CandidateMessageDefinition {
  const char *name;
  std::uint32_t identifier;
  std::uint8_t expected_dlc;
  std::optional<vehicle_core::Microseconds> expected_period_us;
  std::optional<vehicle_core::Microseconds> freshness_timeout_us;
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
  vehicle_core::SignalUnit unit;
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
    vehicle_core::SignalUnit::RevolutionsPerMinute,
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
    vehicle_core::SignalUnit::KilometresPerHour,
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
    vehicle_core::SignalUnit::None,
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
    vehicle_core::SignalUnit::None,
    0.0F,
    15.0F,
    "7=Unknown_7; 8=Unknown_8; 9=Unknown_9; 10=Unknown_10; 11=Unknown_11; 12=Unknown_12; "
    "13=Unknown_13; 15=Shifting",
    36,
    CandidateSignalDefinition::ByteOrder::Motorola,
    "0=P_or_N; 1=1st; 2=2nd; 3=3rd; 4=4th; 5=5th; 6=6th; 7=Unknown_7; 8=Unknown_8; 9=Unknown_9; "
    "10=Unknown_10; 11=Unknown_11; 12=Unknown_12; 13=Unknown_13; 14=Reverse; 15=Shifting",
    kCaptureConfirmedProvenance};

#define MAZDA_BOOLEAN_DEFINITION(symbol, label, identifier_value, bit, dbc_bit, order, table)      \
  inline constexpr CandidateSignalDefinition symbol {                                              \
    label, identifier_value, bit, 1, 1.0F, 0.0F, vehicle_core::SignalUnit::Boolean, 0.0F, 1.0F,    \
        "none", dbc_bit, CandidateSignalDefinition::ByteOrder::order, table,                       \
        kCaptureConfirmedProvenance                                                                \
  }

MAZDA_BOOLEAN_DEFINITION(kLiftgateOpenDefinition, "Liftgate_Open", kDoorsId, 32, 32, Motorola,
                         "0=Closed; 1=Open");
MAZDA_BOOLEAN_DEFINITION(kRearRightDoorOpenDefinition, "RearRightDoor_Open", kDoorsId, 34, 34,
                         Motorola, "0=Closed; 1=Open");
MAZDA_BOOLEAN_DEFINITION(kRearLeftDoorOpenDefinition, "RearLeftDoor_Open", kDoorsId, 35, 35,
                         Motorola, "0=Closed; 1=Open");
MAZDA_BOOLEAN_DEFINITION(kFrontLeftDoorOpenRhdDefinition, "FrontLeftDoor_Open_RHD", kDoorsId, 36,
                         36, Motorola, "0=Closed; 1=Open");
inline constexpr const CandidateSignalDefinition &kFrontLeftDoorOpenDefinition =
    kFrontLeftDoorOpenRhdDefinition;
MAZDA_BOOLEAN_DEFINITION(kFrontRightDoorOpenRhdDefinition, "FrontRightDoor_Open_RHD", kDoorsId, 37,
                         37, Motorola, "0=Closed; 1=Open");
MAZDA_BOOLEAN_DEFINITION(kDoorsUnlockedDefinition, "DoorsUnlocked", kDoorsId, 30, 30, Motorola,
                         "0=Locked; 1=Unlocked");
MAZDA_BOOLEAN_DEFINITION(kLeftIndicatorLampDefinition, "LeftIndicatorLamp", kBlinkInfoId, 18, 18,
                         Intel, "0=Off; 1=On");
MAZDA_BOOLEAN_DEFINITION(kRightIndicatorLampDefinition, "RightIndicatorLamp", kBlinkInfoId, 19, 19,
                         Motorola, "0=Off; 1=On");
MAZDA_BOOLEAN_DEFINITION(kWiperLowDefinition, "WiperLow", kBlinkInfoId, 33, 33, Motorola,
                         "0=Off; 1=On");
MAZDA_BOOLEAN_DEFINITION(kHazardDefinition, "HazardSwitch", kTurnSwitchId, 10, 10, Motorola,
                         "0=Off; 1=On");
MAZDA_BOOLEAN_DEFINITION(kTurnRightSwitchDefinition, "RightIndicatorSwitch", kTurnSwitchId, 12, 12,
                         Motorola, "0=Off; 1=On");
MAZDA_BOOLEAN_DEFINITION(kTurnLeftSwitchDefinition, "LeftIndicatorSwitch", kTurnSwitchId, 13, 13,
                         Motorola, "0=Off; 1=On");

#undef MAZDA_BOOLEAN_DEFINITION

inline constexpr CandidateSignalDefinition kFrontWiperDefinition{
    "FrontWiper",
    kTurnSwitchId,
    20,
    2,
    1.0F,
    0.0F,
    vehicle_core::SignalUnit::None,
    0.0F,
    3.0F,
    "none",
    21,
    CandidateSignalDefinition::ByteOrder::Motorola,
    "0=Off; 1=On; 2=High; 3=Intermittent",
    kCaptureConfirmedProvenance};

} // namespace mazda::candidate
