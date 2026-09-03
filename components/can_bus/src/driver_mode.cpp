#include "can_bus/mode.hpp"

namespace can_bus::internal {

twai_mode_t driver_mode() noexcept {
#if defined(TCAN485_BENCH_ACK_ONLY) && defined(TCAN485_BENCH_TARGET)
  // Normal mode is intentionally available only to the separately named,
  // physically isolated ACK-only bench project. The component still has no
  // transmit queue or public frame-transmission operation.
  return TWAI_MODE_NORMAL;
#else
  // Any missing or ambiguous build guard fails closed to the vehicle-safe
  // mode. This is also the mode used by the approved vehicle exporter.
  return TWAI_MODE_LISTEN_ONLY;
#endif
}

} // namespace can_bus::internal
