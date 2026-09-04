#include "local_argb/local_argb.h"

#include "board/board_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "led_strip.h"

#include <cstdint>

namespace local_argb {
namespace {

constexpr char kTag[] = "local_argb";
constexpr std::uint32_t kRmtResolutionHz = 10'000'000;
constexpr UBaseType_t kWorkerPriority = tskIDLE_PRIORITY + 2;
constexpr std::uint32_t kWorkerStackDepth = 4096;
constexpr TickType_t kWorkerPollTicks = pdMS_TO_TICKS(10) == 0 ? 1 : pdMS_TO_TICKS(10);

class LedStripSink final : public PixelSink {
public:
  void set_handle(const led_strip_handle_t handle) noexcept { handle_ = handle; }

  bool write(const Rgb color) noexcept override {
    if (handle_ == nullptr ||
        led_strip_set_pixel(handle_, 0, color.red, color.green, color.blue) != ESP_OK) {
      return false;
    }
    return led_strip_refresh(handle_) == ESP_OK;
  }

private:
  led_strip_handle_t handle_{nullptr};
};

LedStripSink g_sink;
Controller g_controller{g_sink};
led_strip_handle_t g_strip{nullptr};
StaticQueue_t g_queue_storage{};
std::uint8_t g_queue_buffer[sizeof(SemanticSnapshot)]{};
QueueHandle_t g_queue{nullptr};
TaskHandle_t g_worker{nullptr};
bool g_started{false};

vehicle_core::MonotonicTimestamp now_us() noexcept {
  return static_cast<vehicle_core::MonotonicTimestamp>(esp_timer_get_time());
}

void worker(void *) noexcept {
  for (;;) {
    SemanticSnapshot snapshot{};
    if (xQueueReceive(g_queue, &snapshot, kWorkerPollTicks) == pdTRUE) {
      if (!g_controller.apply(snapshot, now_us())) {
        ESP_LOGE(kTag, "pixel write failed; fail-off clear scheduled for retry");
      }
    } else if (!g_controller.tick(now_us())) {
      ESP_LOGE(kTag, "pixel fail-off clear retry failed");
    }
  }
}

} // namespace

bool start() noexcept {
  if (g_started) {
    return true;
  }
  static_assert(board::kWeActCan485V11.onboard_rgb.data == 4,
                "local ARGB is fixed to the WeAct V1.1 onboard pixel");
  static_assert(board::kWeActCan485V11.onboard_rgb.pixel_count == 1,
                "local ARGB supports exactly one onboard pixel");

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
    return false;
  }

  g_queue = xQueueCreateStatic(1, sizeof(SemanticSnapshot), g_queue_buffer, &g_queue_storage);
  if (g_queue == nullptr || xTaskCreate(worker, "local_argb", kWorkerStackDepth, nullptr,
                                        kWorkerPriority, &g_worker) != pdPASS) {
    (void)g_sink.write(kBlack);
    (void)led_strip_del(g_strip);
    g_strip = nullptr;
    g_sink.set_handle(nullptr);
    g_queue = nullptr;
    return false;
  }
  g_started = true;
  return true;
}

bool submit(const SemanticSnapshot snapshot) noexcept {
  return g_started && g_queue != nullptr && xQueueOverwrite(g_queue, &snapshot) == pdPASS;
}

void fail_off() noexcept {
  const SemanticSnapshot snapshot{vehicle_core::TurnState::Unknown,
                                  vehicle_core::SignalStatus::Unknown, now_us(),
                                  SemanticHealth::CanOffline};
  if (!submit(snapshot)) {
    (void)g_sink.write(kBlack);
  }
}

} // namespace local_argb
