#include "vehicle_core/vehicle_core.hpp"

namespace vehicle_core {

bool RawCanFrame::is_valid() const noexcept {
  if (dlc > kCanClassicPayloadBytes) {
    return false;
  }
  if (identifier_format == CanIdentifierFormat::Standard) {
    return identifier <= 0x7ffU;
  }
  return identifier <= 0x1fffffffU;
}

bool library_is_available() noexcept { return true; }

} // namespace vehicle_core
