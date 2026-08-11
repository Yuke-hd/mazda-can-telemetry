#include "board/board_config.h"
#include "esp_log.h"

namespace {
constexpr char kTag[] = "tcan485";
}

extern "C" void app_main(void) {
  if (!board::initialize_safe_defaults()) {
    ESP_LOGE(kTag, "board safe-default initialization failed; refusing to start");
    return;
  }

  ESP_LOGI(kTag,
           "T-CAN485 safe defaults applied; vehicle CAN remains listen-only and no CAN transmit API "
           "is available");
}
