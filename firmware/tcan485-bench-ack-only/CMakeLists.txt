cmake_minimum_required(VERSION 3.16)

# This project name is part of the artifact safety boundary. Do not rename it
# to a vehicle/product target or use its output outside an isolated bench.
# Its bench mode comes from the bench_can_ack component, never a shared mode
# selector or project-wide compile definition.
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../components/board"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/can_bus"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/bench_can_ack"
    "${CMAKE_CURRENT_LIST_DIR}/../../lib/vehicle_core")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tcan485_bench_ack_only)
