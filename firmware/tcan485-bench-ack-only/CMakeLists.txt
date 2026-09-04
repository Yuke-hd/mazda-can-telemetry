cmake_minimum_required(VERSION 3.16)

# This project name is part of the artifact safety boundary. Do not rename it
# to a vehicle/product target or use its output outside an isolated bench.
set(TCAN485_BENCH_ACK_ONLY ON CACHE BOOL "Enable the isolated bench ACK target" FORCE)
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../components"
    "${CMAKE_CURRENT_LIST_DIR}/../../lib/vehicle_core")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tcan485_bench_ack_only)
