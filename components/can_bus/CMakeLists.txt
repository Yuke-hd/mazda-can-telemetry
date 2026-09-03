idf_component_register(
  SRCS "src/can_bus.cpp" "src/driver_mode.cpp"
  INCLUDE_DIRS "include"
  PRIV_INCLUDE_DIRS "private_include"
  REQUIRES board vehicle_core esp_driver_twai esp_timer
  PRIV_REQUIRES driver
)

# This variable is set only by firmware/tcan485-bench-ack-only/CMakeLists.txt.
# A missing variable deliberately leaves the component in the strict mode.
if(TCAN485_BENCH_ACK_ONLY)
  if(NOT PROJECT_NAME STREQUAL "tcan485_bench_ack_only")
    message(FATAL_ERROR "BENCH_ACK_ONLY is reserved for the named isolated bench project")
  endif()
  target_compile_definitions(${COMPONENT_LIB} PRIVATE
    TCAN485_BENCH_ACK_ONLY=1
    TCAN485_BENCH_TARGET=1
  )
endif()
