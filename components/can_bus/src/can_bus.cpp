#include "can_bus/can_bus.h"

#include <algorithm>
#include <atomic>
#include <cstdint>

#include "can_bus/configuration.hpp"
#include "can_bus/driver_binding.hpp"
#include "can_bus/frame_ring.hpp"
#include "can_bus/lifecycle.hpp"
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
std::atomic<bool> g_receive_requested{false};
internal::LifecycleController g_lifecycle;
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

void latch_terminal_fault() noexcept {
  // Stop the receive loop before waking the consumer. The consumer observes
  // the latched state after taking the token and therefore cannot mistake a
  // terminal driver error for an idle timeout or an empty ring.
  g_receive_requested.store(false, std::memory_order_release);
  if (g_lifecycle.latch_fault() && g_available != nullptr) {
    (void)xSemaphoreGive(g_available);
  }
}

[[nodiscard]] bool collect_alerts() noexcept {
  std::uint32_t alerts = 0;
  const esp_err_t result = twai_read_alerts(&alerts, 0);
  if (result == ESP_ERR_TIMEOUT) {
    // No pending alert is ordinary idle operation when this nonblocking
    // status poll races an otherwise healthy receive interval.
    return true;
  }
  if (result != ESP_OK) {
    latch_terminal_fault();
    return false;
  }
  if ((alerts & TWAI_ALERT_BUS_OFF) != 0U) {
    // A strict listener should not influence the bus or normally enter bus-off.
    // Record the unexpected driver state separately from actual controller
    // resets, latch the fault, and never attempt active recovery.
    g_frames.record_bus_off();
    latch_terminal_fault();
    return false;
  }
  return true;
}

[[nodiscard]] bool collect_driver_status() noexcept {
  twai_status_info_t status{};
  if (twai_get_status_info(&status) != ESP_OK) {
    latch_terminal_fault();
    return false;
  }
  g_frames.record_bus_error(
      internal::counter_delta(status.bus_error_count, g_last_driver_bus_errors));
  g_frames.record_driver_rx_missed(
      internal::counter_delta(status.rx_missed_count, g_last_driver_rx_missed) +
      internal::counter_delta(status.rx_overrun_count, g_last_driver_rx_overrun));
  g_last_driver_bus_errors = status.bus_error_count;
  g_last_driver_rx_missed = status.rx_missed_count;
  g_last_driver_rx_overrun = status.rx_overrun_count;
  return true;
}

void receive_task(void *) noexcept {
  while (g_receive_requested.load(std::memory_order_acquire)) {
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
    } else if (result != ESP_ERR_TIMEOUT) {
      // ESP_ERR_TIMEOUT is ordinary idle operation. Every other receive
      // result is terminal for this run; do not spin a high-priority task on
      // an immediately failing driver call.
      latch_terminal_fault();
      break;
    }
    if (!collect_driver_status() || !collect_alerts()) {
      break;
    }
  }
  g_lifecycle.acknowledge_task();
  (void)xSemaphoreGive(g_task_stopped);
  vTaskDelete(nullptr);
}

} // namespace

Result start(const Configuration &configuration) noexcept {
  if (!internal::is_configuration_valid(configuration)) {
    return Result::kInvalidConfiguration;
  }
  if (!g_lifecycle.begin_start()) {
    switch (g_lifecycle.state()) {
    case LifecycleState::kRunning:
      return Result::kAlreadyStarted;
    case LifecycleState::kStopping:
      return Result::kStopping;
    case LifecycleState::kFaulted:
      return Result::kFaulted;
    case LifecycleState::kStopped:
      // A concurrent cleanup may have completed between begin_start() and
      // this read. Report the conservative state to the caller.
      return Result::kNotStarted;
    }
  }

  if (g_available == nullptr) {
    g_available = xSemaphoreCreateCountingStatic(kQueueCapacity, 0, &g_available_storage);
  }
  if (g_task_stopped == nullptr) {
    g_task_stopped = xSemaphoreCreateBinaryStatic(&g_task_stopped_storage);
  }
  if (g_available == nullptr || g_task_stopped == nullptr) {
    g_lifecycle.abort_start();
    return Result::kTaskFailure;
  }

  // A stopped acquisition may leave accepted frames queued. They are owned by
  // the previous interval and must not be signalled into the next one.
  while (xSemaphoreTake(g_available, 0) == pdTRUE) {
  }
  while (xSemaphoreTake(g_task_stopped, 0) == pdTRUE) {
  }

  twai_general_config_t general{};
  // Mode, CAN pins, and target-specific safety policy are supplied by the
  // application-facing binding component selected by the firmware project.
  internal::configure_driver(general);
  general.rx_queue_len = kQueueCapacity;
  general.alerts_enabled = TWAI_ALERT_BUS_ERROR | TWAI_ALERT_RX_QUEUE_FULL |
                           TWAI_ALERT_ABOVE_ERR_WARN | TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_OFF;
  const twai_timing_config_t timing = timing_for(configuration.bitrate_bps);
  const twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&general, &timing, &filter) != ESP_OK) {
    g_lifecycle.abort_start();
    return Result::kDriverFailure;
  }
  g_lifecycle.mark_driver_installed();
  if (twai_start() != ESP_OK) {
    if (twai_driver_uninstall() == ESP_OK) {
      g_lifecycle.mark_driver_uninstalled();
      g_lifecycle.reconcile();
    } else {
      // Ownership is deliberately retained. A later stop() can retry the
      // uninstall instead of allowing start() to install a second driver.
      g_lifecycle.reconcile();
    }
    return Result::kDriverFailure;
  }
  g_lifecycle.mark_driver_started();

  g_frames.clear();
  g_bus_id = configuration.bus_id;
  const bool had_successful_start = g_has_started_before;
  if (had_successful_start) {
    g_frames.record_controller_reset();
  }
  g_last_driver_bus_errors = 0;
  g_last_driver_rx_missed = 0;
  g_last_driver_rx_overrun = 0;
  g_receive_requested.store(true, std::memory_order_release);
  // Reserve task ownership before creating it. An injected driver can fail
  // immediately (or a real task can fault before xTaskCreate returns), and
  // the acknowledgement must not be overwritten by a late ownership mark.
  g_lifecycle.mark_task_created();
  if (xTaskCreate(receive_task, kReceiveTaskName, kReceiveTaskStackBytes, nullptr,
                  kReceiveTaskPriority, &g_receive_task) != pdPASS) {
    g_receive_requested.store(false, std::memory_order_release);
    g_lifecycle.acknowledge_task();
    (void)g_lifecycle.begin_stop();
    if (twai_stop() == ESP_OK) {
      g_lifecycle.mark_driver_stopped();
      if (twai_driver_uninstall() == ESP_OK) {
        g_lifecycle.mark_driver_uninstalled();
      }
    }
    g_lifecycle.reconcile();
    g_receive_task = nullptr;
    return Result::kTaskFailure;
  }
  g_has_started_before = true;
  return Result::kOk;
}

Result stop() noexcept {
  if (!g_lifecycle.begin_stop()) {
    return Result::kNotStarted;
  }
  g_receive_requested.store(false, std::memory_order_release);

  bool driver_stopped = !g_lifecycle.driver_started();
  if (!driver_stopped) {
    driver_stopped = twai_stop() == ESP_OK;
    if (driver_stopped) {
      g_lifecycle.mark_driver_stopped();
    }
  }

  bool task_stopped = !g_lifecycle.task_owned();
  if (!task_stopped) {
    task_stopped = xSemaphoreTake(g_task_stopped, pdMS_TO_TICKS(100)) == pdTRUE;
    if (task_stopped) {
      // The task clears ownership before giving this semaphore. Keep this
      // acknowledgement idempotent for fault-injection adapters and old IDF
      // ports that signal before their final bookkeeping.
      g_lifecycle.acknowledge_task();
    }
  } else {
    // A terminal fault may have acknowledged before stop() was called.
    (void)xSemaphoreTake(g_task_stopped, 0);
  }
  if (task_stopped) {
    g_receive_task = nullptr;
  }

  bool driver_uninstalled = !g_lifecycle.driver_installed();
  if (driver_stopped && task_stopped && !driver_uninstalled) {
    driver_uninstalled = twai_driver_uninstall() == ESP_OK;
    if (driver_uninstalled) {
      g_lifecycle.mark_driver_uninstalled();
    }
  }

  g_lifecycle.reconcile();
  if (driver_stopped && task_stopped && driver_uninstalled) {
    // Drain any fault wakeup and accepted frames from the completed run. The
    // ring itself remains owned by the bus and is reset on the next start.
    while (g_available != nullptr && xSemaphoreTake(g_available, 0) == pdTRUE) {
    }
    return Result::kOk;
  }
  if (!task_stopped) {
    return Result::kTaskFailure;
  }
  return Result::kDriverFailure;
}

LifecycleState lifecycle() noexcept { return g_lifecycle.state(); }

Result receive(vehicle_core::RawCanFrame &frame, const std::uint32_t timeout_ms) noexcept {
  switch (g_lifecycle.state()) {
  case LifecycleState::kStopped:
    return Result::kNotStarted;
  case LifecycleState::kStopping:
    return Result::kStopping;
  case LifecycleState::kFaulted:
    return Result::kFaulted;
  case LifecycleState::kRunning:
    break;
  }
  const TickType_t wait = timeout_ms == 0 ? 0 : std::max<TickType_t>(1, pdMS_TO_TICKS(timeout_ms));
  if (xSemaphoreTake(g_available, wait) != pdTRUE) {
    // A terminal fault may race the end of the bounded wait. Report the
    // latched outcome rather than making the caller wait for another retry.
    switch (g_lifecycle.state()) {
    case LifecycleState::kStopped:
      return Result::kNotStarted;
    case LifecycleState::kStopping:
      return Result::kStopping;
    case LifecycleState::kFaulted:
      return Result::kFaulted;
    case LifecycleState::kRunning:
      break;
    }
    return Result::kTimeout;
  }
  // A fault wakeup uses the same bounded semaphore as frame availability. A
  // second state check separates that wakeup from ordinary idle timeout.
  switch (g_lifecycle.state()) {
  case LifecycleState::kStopped:
    return Result::kNotStarted;
  case LifecycleState::kStopping:
    return Result::kStopping;
  case LifecycleState::kFaulted:
    return Result::kFaulted;
  case LifecycleState::kRunning:
    break;
  }
  return g_frames.pop(frame) ? Result::kOk : Result::kDriverFailure;
}

Statistics statistics(const StatisticsOperation operation) noexcept {
  return g_frames.snapshot(operation);
}

} // namespace can_bus
