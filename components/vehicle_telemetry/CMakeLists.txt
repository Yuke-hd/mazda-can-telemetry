if(COMMAND idf_component_register)
  idf_component_register(INCLUDE_DIRS "include" PRIV_INCLUDE_DIRS "private_include"
                         REQUIRES vehicle_core mazda)
  return()
endif()

# Contract-only host target. Runtime task/lifecycle sources are owned by S2-A.
add_library(vehicle_telemetry_contracts INTERFACE)
target_include_directories(
  vehicle_telemetry_contracts
  INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(vehicle_telemetry_contracts INTERFACE mazda_contracts)
