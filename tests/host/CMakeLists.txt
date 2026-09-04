include(FetchContent)

# Keep the host test dependency reproducible. Upgrade only in a dedicated change.
FetchContent_Declare(
  doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG ae7a13539fb71f270b87eb2e874fbac80bc8dda2
)
FetchContent_MakeAvailable(doctest)

add_executable(vehicle_core_tests
  vehicle_core_tests.cpp
)
target_link_libraries(vehicle_core_tests PRIVATE vehicle_core doctest::doctest)
target_compile_features(vehicle_core_tests PRIVATE cxx_std_17)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(vehicle_core_tests)

add_executable(mazda_candidate_tests mazda_candidate_tests.cpp)
target_link_libraries(mazda_candidate_tests PRIVATE raw_capture doctest::doctest)
target_compile_features(mazda_candidate_tests PRIVATE cxx_std_17)
doctest_discover_tests(mazda_candidate_tests)

add_executable(mazda_turn_tests mazda_turn_tests.cpp)
target_link_libraries(mazda_turn_tests PRIVATE raw_capture doctest::doctest)
target_compile_features(mazda_turn_tests PRIVATE cxx_std_17)
doctest_discover_tests(mazda_turn_tests)

add_executable(capture_reader_tests capture_reader_tests.cpp)
target_link_libraries(capture_reader_tests PRIVATE raw_capture doctest::doctest)
target_compile_features(capture_reader_tests PRIVATE cxx_std_17)
target_compile_definitions(capture_reader_tests PRIVATE
  CAPTURE_FIXTURE_PATH="${CMAKE_SOURCE_DIR}/tests/fixtures/capture/golden-v1.txt")
doctest_discover_tests(capture_reader_tests)

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

add_executable(
  local_argb_tests
  local_argb_tests.cpp
  ${CMAKE_SOURCE_DIR}/components/local_argb/src/local_argb.cpp
)
target_include_directories(
  local_argb_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/components/local_argb/include
    ${CMAKE_SOURCE_DIR}/lib/vehicle_core/include
)
target_link_libraries(local_argb_tests PRIVATE vehicle_core doctest::doctest)
target_compile_features(local_argb_tests PRIVATE cxx_std_17)
doctest_discover_tests(local_argb_tests)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(
  NAME capture_format_fixture
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_SOURCE_DIR}/tools/validate_capture_format.py
          --spec ${CMAKE_SOURCE_DIR}/docs/protocol/raw-capture-format.md
          --fixture ${CMAKE_SOURCE_DIR}/tests/fixtures/capture/golden-v1.txt
)

add_test(
  NAME can_bus_no_transmit_structure
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_SOURCE_DIR}/tools/validate_can_receive_only.py
          --public-header ${CMAKE_SOURCE_DIR}/components/can_bus/include/can_bus/can_bus.h
          --implementation ${CMAKE_SOURCE_DIR}/components/can_bus/src/can_bus.cpp
)

add_test(
  NAME weact_vehicle_artifact_separation
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_SOURCE_DIR}/tools/validate_weact_vehicle_artifacts.py
          --root ${CMAKE_SOURCE_DIR}
)

add_test(
  NAME local_argb_semantic_boundary
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_SOURCE_DIR}/tools/validate_local_argb_boundary.py
          --root ${CMAKE_SOURCE_DIR}
)
