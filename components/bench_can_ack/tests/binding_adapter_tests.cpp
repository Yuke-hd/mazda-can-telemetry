#include "bench_can_ack/bench_can_ack.h"
#include "can_bus/driver_binding.hpp"

#include <cassert>

int main() {
  twai_general_config_t configuration{};
  can_bus::internal::configure_driver(configuration);
  assert(configuration.tx_io == bench_can_ack::kCanPins.tx);
  assert(configuration.rx_io == bench_can_ack::kCanPins.rx);
  assert(configuration.mode == TWAI_MODE_NORMAL);
  assert(configuration.tx_queue_len == 0);
  return 0;
}
