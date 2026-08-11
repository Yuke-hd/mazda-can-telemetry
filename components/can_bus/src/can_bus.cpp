#include "can_bus/can_bus.h"

#include <cstring>

#include "board/board_config.h"
#include "driver/twai.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

namespace {

bool g_started = false;
can_bus_stats_t g_stats{};

} // namespace

extern "C" bool can_bus_start(void) {
  if (g_started) {
    return true;
  }

  twai_general_config_t general =
      TWAI_GENERAL_CONFIG_DEFAULT(BOARD_CAN_TX, BOARD_CAN_RX, TWAI_MODE_LISTEN_ONLY);
  twai_timing_config_t timing = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&general, &timing, &filter) != ESP_OK) {
    ++g_stats.receive_errors;
    return false;
  }
  if (twai_start() != ESP_OK) {
    ++g_stats.receive_errors;
    twai_driver_uninstall();
    return false;
  }

  g_started = true;
  return true;
}

extern "C" void can_bus_stop(void) {
  if (!g_started) {
    return;
  }
  (void)twai_stop();
  (void)twai_driver_uninstall();
  g_started = false;
}

extern "C" bool can_bus_receive(can_bus_frame_t *frame, const uint32_t timeout_ms) {
  if (!g_started || frame == nullptr) {
    return false;
  }

  twai_message_t message{};
  if (twai_receive(&message, pdMS_TO_TICKS(timeout_ms)) != ESP_OK) {
    ++g_stats.receive_errors;
    return false;
  }

  frame->timestamp_us = static_cast<uint64_t>(esp_timer_get_time());
  frame->bus = 0;
  frame->id = message.identifier;
  frame->is_extended = message.extd;
  frame->is_rtr = message.rtr;
  frame->dlc = message.data_length_code > 8 ? 8 : message.data_length_code;
  std::memcpy(frame->data, message.data, frame->dlc);
  ++g_stats.received_frames;
  return true;
}

extern "C" void can_bus_get_stats(can_bus_stats_t *stats) {
  if (stats != nullptr) {
    *stats = g_stats;
  }
}
