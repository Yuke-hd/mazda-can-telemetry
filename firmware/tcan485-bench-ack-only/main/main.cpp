#include "board/board_config.h"
#include "can_bus/can_bus.h"
#include "esp_log.h"

namespace {
constexpr char kTag[] = "tcan485_bench_ack_only";
constexpr char kIsolationWarning[] =
    "BENCH_ACK_ONLY: never connect to a vehicle; isolated bench only";
} // namespace

extern "C" void app_main(void) {
  if (!board::initialize_safe_defaults()) {
    ESP_LOGE(kTag, "BENCH_ACK_ONLY safe-default initialization failed; refusing to start");
    return;
  }

  constexpr can_bus::Configuration configuration{500'000, 0};
  ESP_LOGW(kTag, "%s; LILYGO/TTGO T-CAN485 classic CAN ACK receiver only", kIsolationWarning);
  ESP_LOGI(kTag,
           "BENCH_ACK_ONLY CAN mode: NORMAL for hardware ACK; bitrate=%lu; TX queue disabled; "
           "no frame transmit API",
           static_cast<unsigned long>(configuration.bitrate_bps));
  if (can_bus::start(configuration) != can_bus::Result::kOk) {
    ESP_LOGE(kTag, "BENCH_ACK_ONLY CAN startup failed; refusing to continue");
    return;
  }
  ESP_LOGI(kTag, "BENCH_ACK_ONLY isolated classic-CAN acquisition started");
}
