#include "local_argb/lighting_sink.hpp"
#include "local_argb/local_argb.h"

#include "board/board_config.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "vehicle_core/signal.hpp"

#include <cstdint>

namespace local_argb {
namespace {

constexpr char kTag[] = "local_argb";
constexpr std::uint32_t kRmtResolutionHz = 10'000'000;
constexpr UBaseType_t kWorkerPriority = tskIDLE_PRIORITY + 2;
// The guard performs no LED/RMT work and is above can_rx (MAX-2), so a stuck
// lower-priority LED call cannot prevent the configured reset bound.
constexpr UBaseType_t kSupervisorPriority = configMAX_PRIORITIES - 1;
constexpr std::uint32_t kWorkerStackDepth = 4096;
constexpr std::uint32_t kSupervisorStackDepth = 2048;
constexpr TickType_t kWorkerPollTicks =
    pdMS_TO_TICKS(kSupervisorPollUs / 1'000) == 0 ? 1 : pdMS_TO_TICKS(kSupervisorPollUs / 1'000);

DriverWatchdog g_driver_watchdog{};
WorkerLease g_worker_lease{};
portMUX_TYPE g_watchdog_lock = portMUX_INITIALIZER_UNLOCKED;

vehicle_core::MonotonicTimestamp now_us() noexcept {
  return static_cast<vehicle_core::MonotonicTimestamp>(esp_timer_get_time());
}

void begin_driver_write() noexcept {
  const auto started_us = now_us();
  taskENTER_CRITICAL(&g_watchdog_lock);
  g_driver_watchdog.begin(started_us);
  taskEXIT_CRITICAL(&g_watchdog_lock);
}

void end_driver_write() noexcept {
  taskENTER_CRITICAL(&g_watchdog_lock);
  g_driver_watchdog.end();
  taskEXIT_CRITICAL(&g_watchdog_lock);
}

void arm_worker_lease() noexcept {
  const auto started_us = now_us();
  taskENTER_CRITICAL(&g_watchdog_lock);
  g_worker_lease.arm(started_us);
  taskEXIT_CRITICAL(&g_watchdog_lock);
}

void heartbeat_worker() noexcept {
  const auto progress_us = now_us();
  taskENTER_CRITICAL(&g_watchdog_lock);
  g_worker_lease.heartbeat(progress_us);
  taskEXIT_CRITICAL(&g_watchdog_lock);
}

void disarm_worker_lease() noexcept {
  taskENTER_CRITICAL(&g_watchdog_lock);
  g_worker_lease.disarm();
  taskEXIT_CRITICAL(&g_watchdog_lock);
}

class LedStripSink final : public PixelSink {
public:
  void set_handle(const led_strip_handle_t handle) noexcept { handle_ = handle; }

  bool write(const Rgb color) noexcept override {
    begin_driver_write();
    const bool success =
        handle_ != nullptr &&
        led_strip_set_pixel(handle_, 0, color.red, color.green, color.blue) == ESP_OK &&
        led_strip_refresh(handle_) == ESP_OK;
    end_driver_write();
    return success;
  }

private:
  led_strip_handle_t handle_{nullptr};
};

LedStripSink g_sink;
Controller g_controller{g_sink};
class QueueSink final : public internal::LightingSink {
public:
  bool publish(const internal::LightingCommand &command) noexcept override;
};

QueueSink g_queue_sink;
led_strip_handle_t g_strip{nullptr};
StaticQueue_t g_queue_storage{};
std::uint8_t g_queue_buffer[sizeof(internal::LightingCommand)]{};
QueueHandle_t g_queue{nullptr};
TaskHandle_t g_worker{nullptr};
TaskHandle_t g_supervisor{nullptr};
bool g_started{false};

void worker(void *) noexcept {
  for (;;) {
    heartbeat_worker();
    internal::LightingCommand command{};
    if (xQueueReceive(g_queue, &command, kWorkerPollTicks) == pdTRUE) {
      if (!g_controller.apply(internal::adapt_command(command), now_us())) {
        ESP_LOGE(kTag, "pixel write failed; fail-off clear scheduled for retry");
      }
    } else if (!g_controller.tick(now_us())) {
      ESP_LOGE(kTag, "pixel fail-off clear retry failed");
    }
  }
}

void supervisor(void *) noexcept {
  for (;;) {
    vTaskDelay(kWorkerPollTicks);
    const auto checked_us = now_us();
    taskENTER_CRITICAL(&g_watchdog_lock);
    const bool restart_due =
        g_driver_watchdog.restart_due(checked_us) || g_worker_lease.restart_due(checked_us);
    taskEXIT_CRITICAL(&g_watchdog_lock);
    if (restart_due) {
      // led_strip 3.0.3 waits indefinitely for RMT completion. A reset is the
      // only bounded recovery available without access to its private channel;
      // boot safe-defaults and startup black run again before CAN can restart.
      esp_restart();
    }
  }
}

void stop_supervisor() noexcept {
  if (g_supervisor != nullptr) {
    vTaskDelete(g_supervisor);
    g_supervisor = nullptr;
  }
}

} // namespace

bool QueueSink::publish(const internal::LightingCommand &command) noexcept {
  // xQueueOverwrite never waits: a fresh command replaces obsolete pending
  // state while the worker remains the sole owner of LED/RMT calls.
  return g_started && g_queue != nullptr && xQueueOverwrite(g_queue, &command) == pdPASS;
}

namespace internal {

LightingSink &sink() noexcept { return g_queue_sink; }

} // namespace internal

bool start() noexcept {
  if (g_started) {
    return true;
  }
  static_assert(board::kWeActCan485V11.onboard_rgb.data == 4,
                "local ARGB is fixed to the WeAct V1.1 onboard pixel");
  static_assert(board::kWeActCan485V11.onboard_rgb.pixel_count == 1,
                "local ARGB supports exactly one onboard pixel");

  if (xTaskCreate(supervisor, "argb_guard", kSupervisorStackDepth, nullptr, kSupervisorPriority,
                  &g_supervisor) != pdPASS) {
    return false;
  }

  led_strip_config_t strip_config{};
  strip_config.strip_gpio_num = board::kWeActCan485V11.onboard_rgb.data;
  strip_config.max_leds = board::kWeActCan485V11.onboard_rgb.pixel_count;
  strip_config.led_model = LED_MODEL_WS2812;
  strip_config.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
  strip_config.flags.invert_out = false;

  led_strip_rmt_config_t rmt_config{};
  rmt_config.clk_src = RMT_CLK_SRC_DEFAULT;
  rmt_config.resolution_hz = kRmtResolutionHz;
  rmt_config.mem_block_symbols = 64;
  rmt_config.flags.with_dma = false;
  if (led_strip_new_rmt_device(&strip_config, &rmt_config, &g_strip) != ESP_OK) {
    stop_supervisor();
    return false;
  }
  g_sink.set_handle(g_strip);

  // Send a physical black frame before CAN starts. Retry twice if the driver
  // reports a transient failure; GPIO-low alone cannot clear a latched pixel.
  bool cleared = g_controller.start();
  for (std::uint8_t retry = 0; !cleared && retry < 2; ++retry) {
    cleared = g_controller.tick(now_us());
  }
  if (!cleared) {
    (void)led_strip_del(g_strip);
    g_strip = nullptr;
    g_sink.set_handle(nullptr);
    stop_supervisor();
    return false;
  }

  g_queue =
      xQueueCreateStatic(1, sizeof(internal::LightingCommand), g_queue_buffer, &g_queue_storage);
  const BaseType_t worker_created = g_queue == nullptr
                                        ? pdFAIL
                                        : xTaskCreate(worker, "local_argb", kWorkerStackDepth,
                                                      nullptr, kWorkerPriority, &g_worker);
  if (worker_created == pdPASS) {
    // Task creation and startup black are complete before monitoring begins,
    // so initialization cannot be mistaken for a worker stall.
    arm_worker_lease();
  }
  if (worker_created != pdPASS) {
    disarm_worker_lease();
    (void)g_sink.write(kBlack);
    (void)led_strip_del(g_strip);
    g_strip = nullptr;
    g_sink.set_handle(nullptr);
    g_queue = nullptr;
    stop_supervisor();
    return false;
  }
  g_started = true;
  return true;
}

bool submit(const LightingCommand command) noexcept {
  const auto private_command = internal::adapt_command(command);
  return g_started && g_queue != nullptr && xQueueOverwrite(g_queue, &private_command) == pdPASS;
}

bool submit(const SemanticSnapshot snapshot) noexcept {
  LightingCommand command{};
  command.color = color_for(snapshot, snapshot.turn_last_update_us);
  command.valid_until_us = snapshot.turn_last_update_us + kFailOffTimeoutUs;
  command.actionable = command.color != kBlack && snapshot.health == SemanticHealth::Online &&
                       snapshot.turn_status == vehicle_core::SignalStatus::Valid;
  if (!command.actionable)
    command.color = kBlack;
  return submit(command);
}

void fail_off() noexcept {
  const LightingCommand command{};
  if (!submit(command)) {
    (void)g_sink.write(kBlack);
  }
}

} // namespace local_argb
