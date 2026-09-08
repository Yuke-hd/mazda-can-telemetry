#pragma once

#include <cstdint>

using gpio_num_t = int;

enum twai_mode_t : std::uint8_t {
  TWAI_MODE_NORMAL,
  TWAI_MODE_NO_ACK,
  TWAI_MODE_LISTEN_ONLY,
};

struct twai_general_config_t {
  gpio_num_t tx_io{0};
  gpio_num_t rx_io{0};
  twai_mode_t mode{TWAI_MODE_LISTEN_ONLY};
  std::uint8_t tx_queue_len{0};
};

constexpr twai_general_config_t make_twai_general_config(const gpio_num_t tx, const gpio_num_t rx,
                                                         const twai_mode_t operation_mode) {
  return twai_general_config_t{tx, rx, operation_mode, 0};
}

#define TWAI_GENERAL_CONFIG_DEFAULT(tx, rx, operation_mode)                                        \
  make_twai_general_config(tx, rx, operation_mode)
