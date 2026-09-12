include(FetchContent)

# Keep the host test dependency reproducible. Upgrade only in a dedicated change.
FetchContent_Declare(
  doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  # v2.5.0 raises doctest's CMake minimum to 3.14, which remains compatible
  # with the project's 3.20 minimum and modern CMake 4.x.
  GIT_TAG d44d4f6e66232d716af82f00a063759e9d0e50d6
)
FetchContent_MakeAvailable(doctest)

add_executable(vehicle_core_tests
  vehicle_core_tests.cpp
)
target_link_libraries(vehicle_core_tests PRIVATE vehicle_core doctest::doctest)
target_compile_features(vehicle_core_tests PRIVATE cxx_std_17)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(vehicle_core_tests)

add_executable(telemetry_contract_compile_tests telemetry_contract_compile_tests.cpp)
target_link_libraries(telemetry_contract_compile_tests PRIVATE vehicle_telemetry_contracts
                                                             telemetry_test_support
                                                             doctest::doctest)
target_compile_features(telemetry_contract_compile_tests PRIVATE cxx_std_17)
doctest_discover_tests(telemetry_contract_compile_tests)

add_executable(telemetry_internal_contract_tests telemetry_internal_contract_tests.cpp)
target_link_libraries(telemetry_internal_contract_tests PRIVATE doctest::doctest)
target_include_directories(telemetry_internal_contract_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/components/local_argb/private_include
  ${CMAKE_SOURCE_DIR}/lib/vehicle_core/include
)
target_compile_features(telemetry_internal_contract_tests PRIVATE cxx_std_17)
doctest_discover_tests(telemetry_internal_contract_tests)

add_executable(mazda_candidate_tests mazda_candidate_tests.cpp)
target_link_libraries(mazda_candidate_tests PRIVATE mazda telemetry_test_support doctest::doctest)
target_compile_features(mazda_candidate_tests PRIVATE cxx_std_17)
doctest_discover_tests(mazda_candidate_tests)

add_executable(mazda_turn_tests mazda_turn_tests.cpp)
target_link_libraries(mazda_turn_tests PRIVATE mazda telemetry_test_support doctest::doctest)
target_compile_features(mazda_turn_tests PRIVATE cxx_std_17)
doctest_discover_tests(mazda_turn_tests)

add_executable(mazda_state_tests mazda_state_tests.cpp)
target_link_libraries(mazda_state_tests PRIVATE mazda telemetry_test_support doctest::doctest)
target_compile_features(mazda_state_tests PRIVATE cxx_std_17)
doctest_discover_tests(mazda_state_tests)

add_executable(mazda_availability_tests mazda_availability_tests.cpp)
target_link_libraries(mazda_availability_tests PRIVATE mazda telemetry_test_support doctest::doctest)
target_compile_features(mazda_availability_tests PRIVATE cxx_std_17)
doctest_discover_tests(mazda_availability_tests)

add_executable(board_config_tests board_config_tests.cpp)
target_include_directories(board_config_tests PRIVATE ${CMAKE_SOURCE_DIR}/components/board/include)
target_compile_features(board_config_tests PRIVATE cxx_std_17)
add_test(NAME board_config_tests COMMAND board_config_tests)

add_executable(can_bus_boundary_tests can_bus_boundary_tests.cpp)
target_include_directories(
  can_bus_boundary_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/components/can_bus/include
    ${CMAKE_SOURCE_DIR}/components/can_bus/private_include
    ${CMAKE_SOURCE_DIR}/lib/vehicle_core/include
)
target_link_libraries(can_bus_boundary_tests PRIVATE doctest::doctest)
target_compile_features(can_bus_boundary_tests PRIVATE cxx_std_17)
doctest_discover_tests(can_bus_boundary_tests)

add_executable(can_bus_lifecycle_tests can_bus_lifecycle_tests.cpp)
target_include_directories(
  can_bus_lifecycle_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/components/can_bus/include
    ${CMAKE_SOURCE_DIR}/components/can_bus/private_include
    ${CMAKE_SOURCE_DIR}/lib/vehicle_core/include
)
target_compile_features(can_bus_lifecycle_tests PRIVATE cxx_std_17)
target_link_libraries(can_bus_lifecycle_tests PRIVATE doctest::doctest)
doctest_discover_tests(can_bus_lifecycle_tests)

add_executable(
  local_argb_tests
  local_argb_tests.cpp
  ${CMAKE_SOURCE_DIR}/components/local_argb/src/local_argb.cpp
  ${CMAKE_SOURCE_DIR}/firmware/weact-can485-v1.1/main/semantic_led_policy.cpp
)
target_include_directories(
  local_argb_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/components/local_argb/include
    ${CMAKE_SOURCE_DIR}/firmware/weact-can485-v1.1/main
    ${CMAKE_SOURCE_DIR}/lib/vehicle_core/include
)
target_link_libraries(local_argb_tests PRIVATE mazda doctest::doctest)
target_compile_features(local_argb_tests PRIVATE cxx_std_17)
doctest_discover_tests(local_argb_tests)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(
  NAME architecture_contracts
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_SOURCE_DIR}/tools/check_architecture.py
          --root ${CMAKE_SOURCE_DIR}
          --compiler ${CMAKE_CXX_COMPILER}
          --cmake ${CMAKE_COMMAND}
)

# Required Stage 1.5 gate. The checker compiles each public entry point in
# isolation, verifies the ordinary consumer target's exported include paths,
# and keeps explicit internal access separate from public-header coverage.
# Keep this as the single CTest registration; the host CI jobs invoke CTest
# once per configured build and must not run the checker as a second step.
add_test(
  NAME public_header_boundary
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_SOURCE_DIR}/tools/check_public_headers.py
          --root ${CMAKE_SOURCE_DIR}
          --compiler ${CMAKE_CXX_COMPILER}
          --cmake ${CMAKE_COMMAND}
)

add_test(
  NAME public_header_checker_regression
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_SOURCE_DIR}/tests/header_boundary/check_public_headers_test.py
          --compiler ${CMAKE_CXX_COMPILER}
          --cmake ${CMAKE_COMMAND}
)
