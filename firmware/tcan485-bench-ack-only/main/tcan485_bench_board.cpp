#include "tcan485_bench_board.hpp"

#include "driver/gpio.h"

#include <cstdint>

namespace tcan485_bench_board {
namespace {

constexpr std::int8_t kCanTx = 27;
constexpr std::int8_t kCanRx = 26;
constexpr std::int8_t kCanSpeedMode = 23;
constexpr std::int8_t kSharedBoostEnable = 16;
constexpr std::int8_t kOnboardLedData = 4;

esp_err_t configure_output(const std::int8_t pin, const int level) {
  const gpio_num_t gpio = static_cast<gpio_num_t>(pin);
  esp_err_t result = gpio_reset_pin(gpio);
  if (result == ESP_OK) {
    result = gpio_set_level(gpio, level);
  }
  if (result == ESP_OK) {
    result = gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
  }
  return result;
}

} // namespace

bool initialize() {
  bool ok = true;
  ok = configure_output(kSharedBoostEnable, 0) == ESP_OK && ok;
  ok = configure_output(kOnboardLedData, 0) == ESP_OK && ok;
  ok = configure_output(kCanTx, 1) == ESP_OK && ok;
  ok = configure_output(kCanSpeedMode, 0) == ESP_OK && ok;
  ok = gpio_reset_pin(static_cast<gpio_num_t>(kCanRx)) == ESP_OK && ok;
  ok = gpio_set_direction(static_cast<gpio_num_t>(kCanRx), GPIO_MODE_INPUT) == ESP_OK && ok;
  if (ok) {
    ok = configure_output(kSharedBoostEnable, 1) == ESP_OK;
  }
  if (!ok) {
    disable();
  }
  return ok;
}

void disable() { (void)configure_output(kSharedBoostEnable, 0); }

} // namespace tcan485_bench_board
