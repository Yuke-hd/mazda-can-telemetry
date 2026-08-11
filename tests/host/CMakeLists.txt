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

add_executable(board_config_tests board_config_tests.cpp)
target_include_directories(board_config_tests PRIVATE ${CMAKE_SOURCE_DIR}/components/board/include)
target_compile_features(board_config_tests PRIVATE cxx_std_17)
add_test(NAME board_config_tests COMMAND board_config_tests)
