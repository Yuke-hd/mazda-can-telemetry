#include "can_bus/can_bus.h"

#include "esp_log.h"

namespace {
constexpr char kTag[] = "tcan485";
}

extern "C" void app_main(void) {
  // The product application is intentionally receive-only. Decoder, capture, and
  // export tasks will be added in later tickets behind bounded interfaces.
  if (!can_bus_start()) {
    ESP_LOGE(kTag, "receive-only CAN startup failed");
    return;
  }
  ESP_LOGI(kTag, "T-CAN485 receive-only skeleton started");
}
