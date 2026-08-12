#pragma once

#include <cstddef>
#include <cstdint>

namespace board {

// This record is the only board-specific input consumed by firmware assembly.
// vehicle_core and Mazda decoders must not include this header.
enum class HardwareRevision : std::uint8_t {
  kTcan485VendorMaterialRevisionPending = 0,
};

struct CanPins {
  std::int8_t tx;
  std::int8_t rx;
  std::int8_t speed_mode;
  std::int8_t boost_enable;
};

struct MicroSdPins {
  std::int8_t miso;
  std::int8_t mosi;
  std::int8_t sclk;
  std::int8_t cs;
};

struct ArgbPins {
  std::int8_t data;
};

struct Capabilities {
  HardwareRevision revision;
  CanPins can;
  MicroSdPins micro_sd;
  ArgbPins argb;
  // Vehicle firmware is permanently receive-only. There is intentionally no
  // board API for sending a CAN frame.
  bool vehicle_listen_only;
  bool can_transmit_api_exposed;
};

inline constexpr Capabilities kTcan485{
    HardwareRevision::kTcan485VendorMaterialRevisionPending,
    CanPins{27, 26, 23, 16},
    MicroSdPins{2, 15, 14, 13},
    ArgbPins{4},
    true,
    false,
};

// Classic ESP32 GPIOs are not a contiguous range. GPIO20, GPIO24, and
// GPIO28-31 do not exist; GPIO34-39 exist but are input-only.
constexpr bool is_valid_gpio(const std::int8_t pin) {
  return (pin >= 0 && pin <= 19) || (pin >= 21 && pin <= 23) || (pin >= 25 && pin <= 27) ||
         (pin >= 32 && pin <= 39);
}

constexpr bool is_output_gpio(const std::int8_t pin) { return is_valid_gpio(pin) && pin <= 33; }

constexpr bool pins_are_distinct(const Capabilities &capabilities) {
  const std::int8_t pins[] = {
      capabilities.can.tx,           capabilities.can.rx,        capabilities.can.speed_mode,
      capabilities.can.boost_enable, capabilities.micro_sd.miso, capabilities.micro_sd.mosi,
      capabilities.micro_sd.sclk,    capabilities.micro_sd.cs,   capabilities.argb.data,
  };
  for (std::size_t first = 0; first < sizeof(pins) / sizeof(pins[0]); ++first) {
    for (std::size_t second = first + 1; second < sizeof(pins) / sizeof(pins[0]); ++second) {
      if (pins[first] == pins[second]) {
        return false;
      }
    }
  }
  return true;
}

constexpr bool is_configuration_valid(const Capabilities &capabilities = kTcan485) {
  return is_output_gpio(capabilities.can.tx) && is_valid_gpio(capabilities.can.rx) &&
         is_output_gpio(capabilities.can.speed_mode) &&
         is_output_gpio(capabilities.can.boost_enable) &&
         is_valid_gpio(capabilities.micro_sd.miso) && is_valid_gpio(capabilities.micro_sd.mosi) &&
         is_valid_gpio(capabilities.micro_sd.sclk) && is_valid_gpio(capabilities.micro_sd.cs) &&
         is_output_gpio(capabilities.argb.data) && pins_are_distinct(capabilities) &&
         capabilities.vehicle_listen_only && !capabilities.can_transmit_api_exposed;
}

static_assert(is_configuration_valid(), "T-CAN485 board configuration must be safe and complete");

// Apply the electrical safe state before any peripheral driver is started:
// CAN TX is recessive, CAN speed mode is high-speed select (low), the CAN/RS485
// boost supply is disabled, and the onboard LED data line is low (off).
// Returns false if any ESP-IDF GPIO operation fails. No CAN driver is started.
bool initialize_safe_defaults();

// Enable or disable the board's shared CAN/RS485 boost supply. CAN startup
// enables it only after safe defaults are applied; stop and every startup
// failure disable it again. Returns false if the GPIO operation fails.
bool set_can_transceiver_power(bool enabled);

} // namespace board
