cmake_minimum_required(VERSION 3.16)

# This is the sole vehicle/product exporter configuration. The legacy T-CAN485
# ACK receiver remains a separate isolated-bench project.
set(TCAN485_BENCH_ACK_ONLY OFF CACHE BOOL "Enable the isolated bench ACK target" FORCE)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../components"
    "${CMAKE_CURRENT_LIST_DIR}/../../lib/vehicle_core")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(weact_can485_v11_vehicle_listen_only)
