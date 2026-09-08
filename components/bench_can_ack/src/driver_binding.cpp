#include "bench_can_ack/bench_can_ack.h"

#include "can_bus/driver_binding.hpp"

namespace can_bus::internal {

void configure_driver(twai_general_config_t &configuration) noexcept {
  configuration = TWAI_GENERAL_CONFIG_DEFAULT(static_cast<gpio_num_t>(bench_can_ack::kCanPins.tx),
                                              static_cast<gpio_num_t>(bench_can_ack::kCanPins.rx),
                                              TWAI_MODE_NORMAL);
  // The bench exists to observe hardware ACKs; the application still has no
  // public API for transmitting a data frame.
  configuration.tx_queue_len = 0;
}

} // namespace can_bus::internal
