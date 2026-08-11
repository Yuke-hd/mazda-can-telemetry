#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint64_t timestamp_us;
  uint8_t bus;
  uint32_t id;
  bool is_extended;
  bool is_rtr;
  uint8_t dlc;
  uint8_t data[8];
} can_bus_frame_t;

typedef struct {
  uint32_t received_frames;
  uint32_t receive_errors;
  uint32_t queue_overflows;
} can_bus_stats_t;

// The vehicle API is deliberately limited to lifecycle, receive, and statistics.
bool can_bus_start(void);
void can_bus_stop(void);
bool can_bus_receive(can_bus_frame_t* frame, uint32_t timeout_ms);
void can_bus_get_stats(can_bus_stats_t* stats);

#ifdef __cplusplus
}
#endif
