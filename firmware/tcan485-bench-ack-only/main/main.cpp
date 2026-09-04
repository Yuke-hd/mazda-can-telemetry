#include "board/board_config.h"
#include "can_bus/can_bus.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstddef>
#include <cstdint>

namespace {
constexpr char kTag[] = "tcan485_bench_ack_only";
constexpr char kIsolationWarning[] =
    "BENCH_ACK_ONLY: never connect to a vehicle; isolated bench only";
constexpr std::size_t kMaxFramesPerBatch = 16;
constexpr TickType_t kSummaryPeriodTicks = pdMS_TO_TICKS(1'000);
constexpr TickType_t kSchedulerDelayTicks = pdMS_TO_TICKS(10) == 0 ? 1 : pdMS_TO_TICKS(10);

struct FrameMetadata {
  bool extended{false};
  std::uint32_t identifier{0};
  std::uint8_t dlc{0};
  std::uint8_t bus_id{0};
};

FrameMetadata metadata_from(const vehicle_core::RawCanFrame &frame) {
  return FrameMetadata{frame.is_extended(), frame.identifier, frame.dlc, frame.bus_id};
}

void log_frame_summary(const std::uint64_t total_count, const std::uint32_t interval_count,
                       const FrameMetadata &latest) {
  ESP_LOGI(kTag,
           "BENCH_ACK_ONLY frames received: total=%llu interval=%lu latest_format=%s "
           "latest_id=0x%lx latest_dlc=%u latest_bus=%u",
           static_cast<unsigned long long>(total_count), static_cast<unsigned long>(interval_count),
           latest.extended ? "extended" : "standard", static_cast<unsigned long>(latest.identifier),
           static_cast<unsigned>(latest.dlc), static_cast<unsigned>(latest.bus_id));
}
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
  std::uint32_t interval_count = 0;
  FrameMetadata latest{};
  TickType_t summary_deadline = xTaskGetTickCount() + kSummaryPeriodTicks;
  for (;;) {
    bool received_batch = false;
    for (std::size_t batch_index = 0; batch_index < kMaxFramesPerBatch; ++batch_index) {
      vehicle_core::RawCanFrame frame{};
      const can_bus::Result result = can_bus::receive(frame, batch_index == 0 ? 1'000 : 0);
      if (result == can_bus::Result::kTimeout) {
        break;
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
      ++interval_count;
      latest = metadata_from(frame);
      if (received_count == 1) {
        ESP_LOGI(kTag,
                 "BENCH_ACK_ONLY first frame received: count=1 format=%s id=0x%lx dlc=%u "
                 "bus=%u",
                 latest.extended ? "extended" : "standard",
                 static_cast<unsigned long>(latest.identifier), static_cast<unsigned>(latest.dlc),
                 static_cast<unsigned>(latest.bus_id));
      }
      received_batch = true;
    }

    const TickType_t now = xTaskGetTickCount();
    if (received_batch && static_cast<int32_t>(now - summary_deadline) >= 0) {
      log_frame_summary(received_count, interval_count, latest);
      interval_count = 0;
      summary_deadline = now + kSummaryPeriodTicks;
    }

    // A permanently non-empty RX queue must not starve IDLE0/the task
    // watchdog. Delay after every bounded batch; taskYIELD alone is not enough.
    if (received_batch) {
      vTaskDelay(kSchedulerDelayTicks);
    }
  }
}
