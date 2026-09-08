cmake_minimum_required(VERSION 3.20)

project(bench_can_ack_binding_adapter_tests LANGUAGES CXX)

add_executable(bench_can_ack_binding_adapter_tests
  ../src/driver_binding.cpp
  binding_adapter_tests.cpp
)
target_include_directories(bench_can_ack_binding_adapter_tests PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/../../can_bus/tests/fake_idf
  ${CMAKE_CURRENT_LIST_DIR}/../../can_bus/include
  ${CMAKE_CURRENT_LIST_DIR}/../include
)
target_compile_features(bench_can_ack_binding_adapter_tests PRIVATE cxx_std_17)

enable_testing()
add_test(NAME bench_can_ack_binding_adapter_tests
         COMMAND bench_can_ack_binding_adapter_tests)
