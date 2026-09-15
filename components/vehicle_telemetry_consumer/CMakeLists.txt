if(COMMAND idf_component_register)
  idf_component_register(
    SRCS "src/public_contract_consumer.cpp" "src/local_argb_binding.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS
      "${CMAKE_CURRENT_LIST_DIR}/../../components/vehicle_telemetry/private_include"
      "${CMAKE_CURRENT_LIST_DIR}/../../lib/mazda/internal_include"
    REQUIRES vehicle_telemetry
    PRIV_REQUIRES local_argb local_argb_sink_contract vehicle_lighting_policy
  )
  return()
endif()

# Compile the same assembly adapter in the portable host suite. The renderer
# sink is intentionally not linked into a host executable; this target is a
# compile-time proof that the firmware-only private handoff remains valid.
add_library(vehicle_telemetry_consumer STATIC
  src/public_contract_consumer.cpp
  src/local_argb_binding.cpp
)
target_include_directories(vehicle_telemetry_consumer
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/vehicle_telemetry/private_include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/mazda/internal_include
)
target_link_libraries(vehicle_telemetry_consumer
  PUBLIC vehicle_telemetry
  PRIVATE vehicle_lighting_policy local_argb local_argb_sink_contract
)
target_compile_features(vehicle_telemetry_consumer PUBLIC cxx_std_17)

if(MSVC)
  target_compile_options(vehicle_telemetry_consumer PRIVATE /W4 /permissive-)
else()
  target_compile_options(vehicle_telemetry_consumer PRIVATE -Wall -Wextra -Wpedantic)
endif()
