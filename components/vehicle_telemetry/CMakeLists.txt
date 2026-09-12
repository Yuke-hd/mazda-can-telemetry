if(COMMAND idf_component_register)
  idf_component_register(SRCS "src/vehicle_telemetry.cpp"
                         INCLUDE_DIRS "include" PRIV_INCLUDE_DIRS "private_include"
                         REQUIRES vehicle_core mazda)
  return()
endif()

# The publication/polling implementation is deliberately a module-local
# target. S2-A can consume its fixed publish/reset handoff without exporting
# private state, task handles, synchronization, or clock seams.
add_library(vehicle_telemetry STATIC
  src/vehicle_telemetry.cpp
)
target_include_directories(
  vehicle_telemetry
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/private_include
)
# The facade's value-only contracts are supplied by mazda; the store's
# implementation-only state/decoder includes remain private above.
target_link_libraries(vehicle_telemetry PUBLIC mazda)
target_compile_features(vehicle_telemetry PUBLIC cxx_std_17)

if(MSVC)
  target_compile_options(vehicle_telemetry PRIVATE /W4 /permissive-)
else()
  target_compile_options(vehicle_telemetry PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Keep the Stage 1.5 target name as a source/build compatibility seam. It
# carries the value-only public include root and the runtime publication
# implementation without propagating this component's private include path.
add_library(vehicle_telemetry_contracts INTERFACE)
target_include_directories(
  vehicle_telemetry_contracts
  INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(vehicle_telemetry_contracts INTERFACE vehicle_telemetry)

if(BUILD_TESTING AND MAZDA_BUILD_HOST_TESTS)
  find_package(Threads REQUIRED)
  add_executable(vehicle_telemetry_publication_tests
    tests/publication_store_tests.cpp
  )
  target_include_directories(vehicle_telemetry_publication_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/private_include
  )
  target_link_libraries(vehicle_telemetry_publication_tests PRIVATE
    vehicle_telemetry
    Threads::Threads
  )
  target_compile_features(vehicle_telemetry_publication_tests PRIVATE cxx_std_17)
  add_test(NAME vehicle_telemetry_publication_tests
           COMMAND vehicle_telemetry_publication_tests)
endif()
