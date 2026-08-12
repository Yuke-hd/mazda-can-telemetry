#include "can_bus/can_bus.h"

#include <algorithm>
#include <atomic>
#include <cstdint>

#include "board/board_config.h"
#include "can_bus/configuration.hpp"
#include "can_bus/frame_ring.hpp"
#include "driver/twai.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace can_bus {
namespace {

constexpr TickType_t kDriverReceivePollTicks = pdMS_TO_TICKS(10);
constexpr UBaseType_t kReceiveTaskPriority = configMAX_PRIORITIES - 2;
constexpr std::uint32_t kReceiveTaskStackBytes = 4096;
constexpr char kReceiveTaskName[] = "can_rx";

internal::FrameRing<kQueueCapacity> g_frames;
StaticSemaphore_t g_available_storage{};
SemaphoreHandle_t g_available{nullptr};
TaskHandle_t g_receive_task{nullptr};
StaticSemaphore_t g_task_stopped_storage{};
SemaphoreHandle_t g_task_stopped{nullptr};
std::atomic<bool> g_running{false};
std::uint8_t g_bus_id{0};
bool g_has_started_before{false};
std::uint32_t g_last_driver_rx_missed{0};
std::uint32_t g_last_driver_rx_overrun{0};
std::uint32_t g_last_driver_bus_errors{0};

[[nodiscard]] twai_timing_config_t timing_for(const std::uint32_t bitrate_bps) noexcept {
  switch (bitrate_bps) {
  case 125'000:
    return TWAI_TIMING_CONFIG_125KBITS();
  case 250'000:
    return TWAI_TIMING_CONFIG_250KBITS();
  case 500'000:
    return TWAI_TIMING_CONFIG_500KBITS();
  case 1'000'000:
    return TWAI_TIMING_CONFIG_1MBITS();
  default:
    // start() validates first; this value can never be installed.
    return TWAI_TIMING_CONFIG_500KBITS();
  }
}

void collect_alerts() noexcept {
  std::uint32_t alerts = 0;
  if (twai_read_alerts(&alerts, 0) != ESP_OK) {
    return;
  }
  if ((alerts & TWAI_ALERT_BUS_OFF) != 0U) {
    // A strict listener should not influence the bus or normally enter bus-off.
    // Record the unexpected controller state; never attempt active recovery.
    g_frames.record_controller_reset();
  }
}

void collect_driver_status() noexcept {
  twai_status_info_t status{};
  if (twai_get_status_info(&status) != ESP_OK) {
    return;
  }
  g_frames.record_bus_error(
      internal::counter_delta(status.bus_error_count, g_last_driver_bus_errors));
  g_frames.record_driver_rx_missed(
      internal::counter_delta(status.rx_missed_count, g_last_driver_rx_missed) +
      internal::counter_delta(status.rx_overrun_count, g_last_driver_rx_overrun));
  g_last_driver_bus_errors = status.bus_error_count;
  g_last_driver_rx_missed = status.rx_missed_count;
  g_last_driver_rx_overrun = status.rx_overrun_count;
}

void receive_task(void *) noexcept {
  while (g_running.load(std::memory_order_acquire)) {
    twai_message_t message{};
    const esp_err_t result = twai_receive(&message, kDriverReceivePollTicks);
    if (result == ESP_OK) {
      vehicle_core::RawCanFrame frame{};
      frame.timestamp_us = static_cast<vehicle_core::MonotonicTimestamp>(esp_timer_get_time());
      frame.bus_id = g_bus_id;
      frame.identifier = message.identifier;
      frame.identifier_format = message.extd ? vehicle_core::CanIdentifierFormat::Extended
                                             : vehicle_core::CanIdentifierFormat::Standard;
      frame.remote_request = message.rtr;
      frame.dlc = message.data_length_code;
      std::copy_n(message.data, vehicle_core::kCanClassicPayloadBytes, frame.data.begin());

      if (!frame.is_valid()) {
        g_frames.record_driver_rx_missed();
      } else if (g_frames.push(frame)) {
        (void)xSemaphoreGive(g_available);
      }
    }
    collect_driver_status();
    collect_alerts();
  }
  (void)xSemaphoreGive(g_task_stopped);
  vTaskDelete(nullptr);
}

} // namespace

Result start(const Configuration &configuration) noexcept {
  if (!internal::is_configuration_valid(configuration)) {
    return Result::kInvalidConfiguration;
  }
  if (g_running.load(std::memory_order_acquire) || g_receive_task != nullptr) {
    return Result::kAlreadyStarted;
  }

  if (g_available == nullptr) {
    g_available = xSemaphoreCreateCountingStatic(kQueueCapacity, 0, &g_available_storage);
  }
  if (g_task_stopped == nullptr) {
    g_task_stopped = xSemaphoreCreateBinaryStatic(&g_task_stopped_storage);
  }
  if (g_available == nullptr || g_task_stopped == nullptr) {
    (void)board::set_can_transceiver_power(false);
    return Result::kTaskFailure;
  }

  // A stopped acquisition may leave accepted frames queued. They are owned by
  // the previous interval and must not be signalled into the next one.
  while (xSemaphoreTake(g_available, 0) == pdTRUE) {
  }

  twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT(
      static_cast<gpio_num_t>(board::kTcan485.can.tx),
      static_cast<gpio_num_t>(board::kTcan485.can.rx), TWAI_MODE_LISTEN_ONLY);
  general.tx_queue_len = 0;
  general.rx_queue_len = kQueueCapacity;
  general.alerts_enabled = TWAI_ALERT_BUS_ERROR | TWAI_ALERT_RX_QUEUE_FULL |
                           TWAI_ALERT_ABOVE_ERR_WARN | TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_OFF;
  const twai_timing_config_t timing = timing_for(configuration.bitrate_bps);
  const twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (!board::set_can_transceiver_power(true)) {
    (void)board::set_can_transceiver_power(false);
    return Result::kDriverFailure;
  }
  if (twai_driver_install(&general, &timing, &filter) != ESP_OK) {
    (void)board::set_can_transceiver_power(false);
    return Result::kDriverFailure;
  }
  if (twai_start() != ESP_OK) {
    (void)twai_driver_uninstall();
    (void)board::set_can_transceiver_power(false);
    return Result::kDriverFailure;
  }

  g_frames.clear();
  g_bus_id = configuration.bus_id;
  const bool had_successful_start = g_has_started_before;
  if (had_successful_start) {
    g_frames.record_controller_reset();
  }
  g_last_driver_bus_errors = 0;
  g_last_driver_rx_missed = 0;
  g_last_driver_rx_overrun = 0;
  g_running.store(true, std::memory_order_release);
  if (xTaskCreate(receive_task, kReceiveTaskName, kReceiveTaskStackBytes, nullptr,
                  kReceiveTaskPriority, &g_receive_task) != pdPASS) {
    g_running.store(false, std::memory_order_release);
    (void)twai_stop();
    (void)twai_driver_uninstall();
    (void)board::set_can_transceiver_power(false);
    g_receive_task = nullptr;
    return Result::kTaskFailure;
  }
  g_has_started_before = true;
  return Result::kOk;
}

Result stop() noexcept {
  if (!g_running.exchange(false, std::memory_order_acq_rel)) {
    return Result::kNotStarted;
  }

  const bool driver_stopped = twai_stop() == ESP_OK;
  if (xSemaphoreTake(g_task_stopped, pdMS_TO_TICKS(100)) != pdTRUE) {
    (void)board::set_can_transceiver_power(false);
    return Result::kTaskFailure;
  }
  g_receive_task = nullptr;
  const bool driver_uninstalled = twai_driver_uninstall() == ESP_OK;
  const bool power_disabled = board::set_can_transceiver_power(false);
  return driver_stopped && driver_uninstalled && power_disabled ? Result::kOk
                                                                : Result::kDriverFailure;
}

Result receive(vehicle_core::RawCanFrame &frame, const std::uint32_t timeout_ms) noexcept {
  if (!g_running.load(std::memory_order_acquire)) {
    return Result::kNotStarted;
  }
  const TickType_t wait = timeout_ms == 0 ? 0 : std::max<TickType_t>(1, pdMS_TO_TICKS(timeout_ms));
  if (xSemaphoreTake(g_available, wait) != pdTRUE) {
    return Result::kTimeout;
  }
  return g_frames.pop(frame) ? Result::kOk : Result::kDriverFailure;
}

Statistics statistics(const StatisticsOperation operation) noexcept {
  return g_frames.snapshot(operation);
}

} // namespace can_bus
