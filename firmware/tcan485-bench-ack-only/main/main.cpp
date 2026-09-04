#include "board/board_config.h"
#include "can_bus/can_bus.h"
#include "esp_log.h"

#include <cstdint>

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

  std::uint64_t received_count = 0;
  for (;;) {
    vehicle_core::RawCanFrame frame{};
    const can_bus::Result result = can_bus::receive(frame, 1'000);
    if (result == can_bus::Result::kTimeout) {
      continue;
    }
    if (result != can_bus::Result::kOk) {
      ESP_LOGE(kTag, "BENCH_ACK_ONLY receive failure (%u); stopping safely",
               static_cast<unsigned>(result));
      const can_bus::Result stop_result = can_bus::stop();
      if (stop_result != can_bus::Result::kOk) {
        ESP_LOGE(kTag, "BENCH_ACK_ONLY stop after receive failure also failed (%u)",
                 static_cast<unsigned>(stop_result));
      }
      return;
    }

    ++received_count;
    ESP_LOGI(kTag, "BENCH_ACK_ONLY frame received: count=%llu format=%s id=0x%lx dlc=%u bus=%u",
             static_cast<unsigned long long>(received_count),
             frame.is_extended() ? "extended" : "standard",
             static_cast<unsigned long>(frame.identifier), static_cast<unsigned>(frame.dlc),
             static_cast<unsigned>(frame.bus_id));
  }
}
