idf_component_register(
  SRCS "main.cpp" "semantic_led_policy.cpp"
  INCLUDE_DIRS "."
  REQUIRES board can_bus local_argb mazda
)
