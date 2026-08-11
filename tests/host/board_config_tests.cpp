#include <cassert>

#include "board/board_config.h"

namespace {

constexpr board::Capabilities kDuplicatePinConfiguration{
    board::HardwareRevision::kTcan485VendorMaterialRevisionPending,
    board::CanPins{27, 26, 23, 16},
    board::MicroSdPins{2, 15, 14, 13},
    board::ArgbPins{13},
    true,
    false,
};

constexpr board::Capabilities kTransmitEnabledConfiguration{
    board::HardwareRevision::kTcan485VendorMaterialRevisionPending,
    board::CanPins{27, 26, 23, 16},
    board::MicroSdPins{2, 15, 14, 13},
    board::ArgbPins{4},
    true,
    true,
};

static_assert(board::is_configuration_valid(), "the vendor pin record must be valid");
static_assert(!board::is_valid_gpio(20), "GPIO20 is not present on classic ESP32");
static_assert(!board::is_valid_gpio(24), "GPIO24 is not present on classic ESP32");
static_assert(!board::is_valid_gpio(28), "GPIO28 is not present on classic ESP32");
static_assert(!board::is_valid_gpio(31), "GPIO31 is not present on classic ESP32");
static_assert(board::is_valid_gpio(34), "GPIO34 exists as an input-only GPIO");
static_assert(!board::is_output_gpio(34), "GPIO34 must not be configured as output");
static_assert(!board::is_configuration_valid(kDuplicatePinConfiguration),
              "duplicate pins must fail closed at compile time");
static_assert(!board::is_configuration_valid(kTransmitEnabledConfiguration),
              "a CAN transmit API must fail closed at compile time");

} // namespace

int main() {
  assert(!board::is_valid_gpio(20));
  assert(!board::is_valid_gpio(24));
  assert(!board::is_valid_gpio(28));
  assert(!board::is_valid_gpio(31));
  assert(board::is_valid_gpio(34));
  assert(!board::is_output_gpio(34));
  assert(board::kTcan485.can.tx == 27);
  assert(board::kTcan485.can.rx == 26);
  assert(board::kTcan485.can.speed_mode == 23);
  assert(board::kTcan485.can.boost_enable == 16);
  assert(board::kTcan485.micro_sd.miso == 2);
  assert(board::kTcan485.micro_sd.mosi == 15);
  assert(board::kTcan485.micro_sd.sclk == 14);
  assert(board::kTcan485.micro_sd.cs == 13);
  assert(board::kTcan485.argb.data == 4);
  assert(board::kTcan485.vehicle_listen_only);
  assert(!board::kTcan485.can_transmit_api_exposed);
  return 0;
}
