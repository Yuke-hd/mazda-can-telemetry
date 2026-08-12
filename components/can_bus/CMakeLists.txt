idf_component_register(
  SRCS "src/can_bus.cpp"
  INCLUDE_DIRS "include"
  PRIV_INCLUDE_DIRS "private_include"
  REQUIRES board vehicle_core esp_driver_twai esp_timer
  PRIV_REQUIRES driver
)
