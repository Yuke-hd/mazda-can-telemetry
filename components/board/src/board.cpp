#include "board/board_config.h"

#include "driver/gpio.h"

namespace board {
namespace {

esp_err_t configure_output(const std::int8_t pin, const int level) {
  const gpio_num_t gpio = static_cast<gpio_num_t>(pin);
  esp_err_t result = gpio_reset_pin(gpio);
  if (result != ESP_OK) {
    return result;
  }

  // Set the output latch before enabling output so the transition cannot drive
  // a dominant CAN bit or an LED pulse during initialization.
  result = gpio_set_level(gpio, level);
  if (result != ESP_OK) {
    return result;
  }
  return gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
}

esp_err_t configure_input(const std::int8_t pin) {
  const gpio_num_t gpio = static_cast<gpio_num_t>(pin);
  esp_err_t result = gpio_reset_pin(gpio);
  if (result != ESP_OK) {
    return result;
  }
  return gpio_set_direction(gpio, GPIO_MODE_INPUT);
}

} // namespace

bool initialize_safe_defaults() {
  if (!is_configuration_valid()) {
    return false;
  }

  // Keep the transceiver supply disabled and the LED off first. Then put CAN
  // TX in the recessive state before it becomes an output. The remaining board
  // pins are configured only after these safety-critical defaults.
  bool ok = true;
  ok = configure_output(kTcan485.can.boost_enable, 0) == ESP_OK && ok;
  ok = configure_output(kTcan485.argb.data, 0) == ESP_OK && ok;
  ok = configure_output(kTcan485.can.tx, 1) == ESP_OK && ok;
  ok = configure_output(kTcan485.can.speed_mode, 0) == ESP_OK && ok;
  ok = configure_input(kTcan485.can.rx) == ESP_OK && ok;
  ok = configure_input(kTcan485.micro_sd.miso) == ESP_OK && ok;
  ok = configure_input(kTcan485.micro_sd.mosi) == ESP_OK && ok;
  ok = configure_input(kTcan485.micro_sd.sclk) == ESP_OK && ok;
  ok = configure_input(kTcan485.micro_sd.cs) == ESP_OK && ok;

  // Best-effort reassertion if a later GPIO operation failed. The caller must
  // refuse to start the application when ok is false.
  if (!ok) {
    (void)configure_output(kTcan485.can.boost_enable, 0);
    (void)configure_output(kTcan485.argb.data, 0);
    (void)configure_output(kTcan485.can.tx, 1);
  }
  return ok;
}

} // namespace board
