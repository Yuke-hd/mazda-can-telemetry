idf_component_register(
  SRCS "src/local_argb.cpp" "src/local_argb_idf.cpp"
  INCLUDE_DIRS "include"
  REQUIRES board vehicle_core esp_timer led_strip
  PRIV_REQUIRES freertos
)
