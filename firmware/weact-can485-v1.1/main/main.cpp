#include "board/board_config.h"
#include "can_bus/can_bus.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "local_argb/local_argb.h"
#include "semantic_led_policy.h"

namespace {
constexpr char kTag[] = "weact_can485_v11";
}

extern "C" void app_main(void) {
  if (!board::initialize_safe_defaults()) {
    ESP_LOGE(kTag, "board safe-default initialization failed; refusing to start");
    return;
  }
  if (!local_argb::start()) {
    ESP_LOGE(kTag, "explicit startup LED clear failed; refusing to start CAN");
    return;
  }

  constexpr can_bus::Configuration configuration{500'000, 0};
  ESP_LOGI(kTag,
           "WeAct CAN485 DevBoard V1.1 vehicle CAN mode: STRICT LISTEN-ONLY; bitrate=%lu; "
           "TX queue disabled; receive API only",
           static_cast<unsigned long>(configuration.bitrate_bps));
  if (can_bus::start(configuration) != can_bus::Result::kOk) {
    local_argb::fail_off();
    ESP_LOGE(kTag, "strict listen-only CAN startup failed; refusing to continue");
    return;
  }
  ESP_LOGI(kTag, "strict listen-only CAN acquisition started");

  weact_app::Context context{};
  local_argb::PublicationPolicy publication{};
  for (;;) {
    vehicle_core::RawCanFrame frame{};
    const can_bus::Result receive_result = can_bus::receive(frame, 50);
    const auto now = static_cast<vehicle_core::MonotonicTimestamp>(esp_timer_get_time());
    if (receive_result == can_bus::Result::kOk) {
      weact_app::process_received_frame(context, frame);
    } else if (receive_result != can_bus::Result::kTimeout) {
      local_argb::fail_off();
      (void)can_bus::stop();
      ESP_LOGE(kTag, "CAN receive failed; LED cleared and acquisition stopped");
      return;
    }

    const auto semantic = weact_app::semantic_snapshot(context, now);
    if (publication.should_publish(semantic, now) && !local_argb::submit(semantic)) {
      local_argb::fail_off();
      (void)can_bus::stop();
      ESP_LOGE(kTag, "semantic LED submission failed; acquisition stopped fail-off");
      return;
    }
  }
}
