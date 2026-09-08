#include "vehicle_core/vehicle_core.hpp"

namespace vehicle_core {

bool RawCanFrame::is_valid() const noexcept {
  if (dlc > kCanClassicPayloadBytes) {
    return false;
  }
  switch (identifier_format) {
  case CanIdentifierFormat::Standard:
    return identifier <= 0x7ffU;
  case CanIdentifierFormat::Extended:
    return identifier <= 0x1fffffffU;
  }
  // The enum is part of the portable wire boundary. Do not silently treat an
  // out-of-enum value as an extended identifier.
  return false;
}

bool library_is_available() noexcept { return true; }

} // namespace vehicle_core
