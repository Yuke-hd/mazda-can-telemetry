cmake_minimum_required(VERSION 3.16)

# This is the sole vehicle/product exporter configuration. The legacy T-CAN485
# ACK receiver remains a separate isolated-bench project selected by its own
# application-facing component.

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../components/board"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/can_bus"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/local_argb"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/vehicle_can_rx"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/vehicle_telemetry"
    "${CMAKE_CURRENT_LIST_DIR}/../../lib/vehicle_core")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(weact_can485_v11_vehicle_listen_only)
