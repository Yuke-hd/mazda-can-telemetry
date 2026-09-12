idf_component_register(
  SRCS "main.cpp" "semantic_led_policy.cpp"
  INCLUDE_DIRS "."
  REQUIRES board can_bus vehicle_can_rx local_argb local_argb_compat mazda vehicle_telemetry_consumer
)
