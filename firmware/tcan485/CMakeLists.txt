cmake_minimum_required(VERSION 3.16)

# This is the vehicle-safe exporter configuration. The bench ACK target is a
# separate ESP-IDF project under firmware/tcan485-bench-ack-only.
set(TCAN485_BENCH_ACK_ONLY OFF CACHE BOOL "Enable the isolated bench ACK target" FORCE)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../components"
    "${CMAKE_CURRENT_LIST_DIR}/../../lib/vehicle_core")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tcan485_vehicle_listen_only)
