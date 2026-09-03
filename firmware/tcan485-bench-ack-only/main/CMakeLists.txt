idf_component_register(
  SRCS "main.cpp"
  INCLUDE_DIRS "."
  REQUIRES board can_bus
)

# Keep the application-side guard explicit as an additional safety check. The
# can_bus component receives the same definitions from the project variable.
target_compile_definitions(${COMPONENT_LIB} PRIVATE
  TCAN485_BENCH_ACK_ONLY=1
  TCAN485_BENCH_TARGET=1
)
