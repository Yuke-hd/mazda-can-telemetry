#include "board/board_config.h"
#include "can_bus/can_bus.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "local_argb/local_argb.h"

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

  vehicle_core::VehicleState state{};
  local_argb::SemanticHealth health = local_argb::SemanticHealth::CanOffline;
  for (;;) {
    vehicle_core::RawCanFrame frame{};
    const can_bus::Result receive_result = can_bus::receive(frame, 50);
    const auto now = static_cast<vehicle_core::MonotonicTimestamp>(esp_timer_get_time());
    if (receive_result == can_bus::Result::kOk) {
      const auto decode_status = vehicle_core::mazda_candidate::decode(frame, state);
      switch (decode_status) {
      case vehicle_core::mazda_candidate::DecodeStatus::Updated:
        health = local_argb::SemanticHealth::Online;
        break;
      case vehicle_core::mazda_candidate::DecodeStatus::Ignored:
        // Unrelated valid traffic is normal and proves that CAN is online. It
        // must not erase a prior decoder error until a valid update arrives.
        if (health == local_argb::SemanticHealth::CanOffline) {
          health = local_argb::SemanticHealth::Online;
        }
        break;
      case vehicle_core::mazda_candidate::DecodeStatus::Invalid:
        health = local_argb::SemanticHealth::DecoderError;
        break;
      }
    } else if (receive_result != can_bus::Result::kTimeout) {
      local_argb::fail_off();
      (void)can_bus::stop();
      ESP_LOGE(kTag, "CAN receive failed; LED cleared and acquisition stopped");
      return;
    }

    const auto semantic = local_argb::from_vehicle_state(state.snapshot(now), health);
    if (!local_argb::submit(semantic)) {
      local_argb::fail_off();
      (void)can_bus::stop();
      ESP_LOGE(kTag, "semantic LED submission failed; acquisition stopped fail-off");
      return;
    }
  }
}
