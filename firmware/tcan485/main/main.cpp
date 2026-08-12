#include "board/board_config.h"
#include "can_bus/can_bus.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "raw_capture/raw_capture.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace {
constexpr char kTag[] = "tcan485";

class CanSource final : public raw_capture::FrameSource {
public:
  bool try_receive(vehicle_core::RawCanFrame &frame) noexcept override {
    return can_bus::receive(frame, 0) == can_bus::Result::kOk;
  }
};

// UART0 is the board's USB-UART bridge. This sink only calls the documented
// non-blocking FIFO API. It retains a partially accepted complete line and
// resumes it on the next call; a line is never duplicated or split into a
// second record. No UART receive API is linked or called.
class UsbUartSink final : public raw_capture::OutputSink {
public:
  bool connected() const noexcept override { return true; }

  raw_capture::WriteResult write(const std::string_view line) noexcept override {
    if (pending_size_ == 0) {
      if (line.size() > pending_.size()) {
        return raw_capture::WriteResult::kError;
      }
      std::copy(line.begin(), line.end(), pending_.begin());
      pending_size_ = line.size();
      pending_offset_ = 0;
    } else if (line.size() != pending_size_ ||
               !std::equal(line.begin(), line.end(), pending_.begin())) {
      return raw_capture::WriteResult::kError;
    }

    const int accepted = uart_tx_chars(UART_NUM_0, pending_.data() + pending_offset_,
                                       static_cast<uint32_t>(pending_size_ - pending_offset_));
    if (accepted == 0) {
      return raw_capture::WriteResult::kWouldBlock;
    }
    if (accepted < 0) {
      return raw_capture::WriteResult::kError;
    }
    pending_offset_ += static_cast<std::size_t>(accepted);
    if (pending_offset_ != pending_size_) {
      return raw_capture::WriteResult::kWouldBlock;
    }
    pending_size_ = 0;
    pending_offset_ = 0;
    return raw_capture::WriteResult::kWritten;
  }

  void discard_partial_line() noexcept override {
    pending_size_ = 0;
    pending_offset_ = 0;
  }

private:
  std::array<char, 512> pending_{};
  std::size_t pending_size_{0};
  std::size_t pending_offset_{0};
};

constexpr raw_capture::Configuration kCaptureConfiguration{
    raw_capture::SessionMetadata{"mcan-tcan485+0.1.0", "tcan485-revA", 500'000, 1'000'000, 0, 0},
    1'000'000, 1'000'000};
raw_capture::Exporter g_exporter{kCaptureConfiguration};
CanSource g_can_source;
UsbUartSink g_usb_sink;
std::uint64_t g_last_can_drops{0};
std::uint64_t g_last_driver_missed{0};

bool initialize_usb_uart() noexcept {
  constexpr int kUsbBaudRate = 115200;
  uart_config_t configuration{};
  configuration.baud_rate = kUsbBaudRate;
  configuration.data_bits = UART_DATA_8_BITS;
  configuration.parity = UART_PARITY_DISABLE;
  configuration.stop_bits = UART_STOP_BITS_1;
  configuration.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  configuration.rx_flow_ctrl_thresh = 0;
  configuration.source_clk = UART_SCLK_DEFAULT;
  if (uart_driver_install(UART_NUM_0, 256, 0, 0, nullptr, 0) != ESP_OK ||
      uart_param_config(UART_NUM_0, &configuration) != ESP_OK ||
      uart_set_pin(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                   UART_PIN_NO_CHANGE) != ESP_OK) {
    (void)uart_driver_delete(UART_NUM_0);
    return false;
  }
  return true;
}

void capture_export_task(void *) noexcept {
  while (true) {
    (void)g_exporter.poll_input(g_can_source, 16);
    const auto can_stats = can_bus::statistics();
    const std::uint64_t app_loss = can_stats.frames_dropped - g_last_can_drops;
    const std::uint64_t driver_loss = can_stats.driver_rx_missed - g_last_driver_missed;
    if (app_loss != 0 || driver_loss != 0) {
      g_exporter.note_dropped_frames(app_loss + driver_loss,
                                     static_cast<std::uint64_t>(esp_timer_get_time()));
      g_last_can_drops = can_stats.frames_dropped;
      g_last_driver_missed = can_stats.driver_rx_missed;
    }
    (void)g_exporter.poll_output(g_usb_sink, static_cast<std::uint64_t>(esp_timer_get_time()), 8);
    if (g_exporter.failed()) {
      (void)can_bus::stop();
      vTaskDelete(nullptr);
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
} // namespace

extern "C" void app_main(void) {
  if (!board::initialize_safe_defaults()) {
    ESP_LOGE(kTag, "board safe-default initialization failed; refusing to start");
    return;
  }

  if (!initialize_usb_uart()) {
    ESP_LOGE(kTag, "output-only USB UART initialization failed; refusing to start CAN");
    return;
  }

  constexpr can_bus::Configuration configuration{500'000, 0};
  ESP_LOGI(kTag,
           "vehicle CAN mode: STRICT LISTEN-ONLY; bitrate=%lu; TX queue disabled; receive API "
           "only",
           static_cast<unsigned long>(configuration.bitrate_bps));
  if (can_bus::start(configuration) != can_bus::Result::kOk) {
    ESP_LOGE(kTag, "strict listen-only CAN startup failed; refusing to continue");
    return;
  }
  ESP_LOGI(kTag, "strict listen-only CAN acquisition started");
  // UART0 is the sole capture writer after this point; suppress logs so they
  // cannot interleave with a partially accepted capture line.
  esp_log_level_set("*", ESP_LOG_NONE);
  if (xTaskCreate(capture_export_task, "capture_export", 4096, nullptr, configMAX_PRIORITIES - 3,
                  nullptr) != pdPASS) {
    ESP_LOGE(kTag, "raw capture task startup failed; CAN remains receive-only");
    (void)can_bus::stop();
  }
}
