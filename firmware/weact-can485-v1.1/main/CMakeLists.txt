idf_component_register(
  SRCS "main.cpp"
  INCLUDE_DIRS "."
  REQUIRES board local_argb vehicle_can_rx vehicle_telemetry vehicle_telemetry_consumer freertos
)
