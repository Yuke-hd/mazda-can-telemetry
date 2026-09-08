idf_component_register(
  SRCS "main.cpp" "tcan485_bench_board.cpp"
  INCLUDE_DIRS "."
  REQUIRES board can_bus bench_can_ack esp_driver_gpio
)
