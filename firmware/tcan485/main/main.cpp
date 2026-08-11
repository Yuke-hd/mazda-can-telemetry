#include "esp_log.h"

namespace {
constexpr char kTag[] = "tcan485";
}

extern "C" void app_main(void) {
  ESP_LOGI(kTag, "T-CAN485 project scaffold; CAN is not initialized");
}
