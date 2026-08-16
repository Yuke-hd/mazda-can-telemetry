idf_component_register(
  SRCS "main.cpp"
  INCLUDE_DIRS "."
  REQUIRES board can_bus raw_capture esp_driver_uart
)
