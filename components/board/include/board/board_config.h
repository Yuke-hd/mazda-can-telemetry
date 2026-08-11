#pragma once

#include "driver/gpio.h"

// Pin map for the T-CAN485 revision documented in AGENTS.md.
#define BOARD_CAN_TX GPIO_NUM_27
#define BOARD_CAN_RX GPIO_NUM_26
#define BOARD_CAN_MODE GPIO_NUM_23
#define BOARD_CAN_BOOST_ENABLE GPIO_NUM_16
#define BOARD_ONBOARD_WS2812 GPIO_NUM_4
