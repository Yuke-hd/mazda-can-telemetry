#include "board/board_config.h"
#include "can_bus/can_bus.h"
#include "esp_log.h"

namespace {
constexpr char kTag[] = "weact_can485_v11";
}

extern "C" void app_main(void) {
  if (!board::initialize_safe_defaults()) {
    ESP_LOGE(kTag, "board safe-default initialization failed; refusing to start");
    return;
  }

  constexpr can_bus::Configuration configuration{500'000, 0};
  ESP_LOGI(kTag,
           "WeAct CAN485 DevBoard V1.1 vehicle CAN mode: STRICT LISTEN-ONLY; bitrate=%lu; "
           "TX queue disabled; receive API only",
           static_cast<unsigned long>(configuration.bitrate_bps));
  if (can_bus::start(configuration) != can_bus::Result::kOk) {
    ESP_LOGE(kTag, "strict listen-only CAN startup failed; refusing to continue");
    return;
  }
  ESP_LOGI(kTag, "strict listen-only CAN acquisition started");
}
