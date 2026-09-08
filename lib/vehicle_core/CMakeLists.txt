if(COMMAND idf_component_register)
  idf_component_register(SRCS "src/vehicle_core.cpp" "src/mazda_candidate.cpp" INCLUDE_DIRS "include")
  return()
endif()

add_library(vehicle_core STATIC
  src/vehicle_core.cpp
  src/mazda_candidate.cpp
)

target_include_directories(vehicle_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(vehicle_core PUBLIC cxx_std_17)

if(MSVC)
  target_compile_options(vehicle_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(vehicle_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Portable latest-state notification delivery.  The implementation is
# header-only so channel value types remain independent of Mazda and RTOS
# headers; this target provides a stable module-local handoff for later service
# composition.
add_library(vehicle_core_notifications INTERFACE)
target_include_directories(vehicle_core_notifications INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(BUILD_TESTING)
  find_package(Threads REQUIRED)
  add_executable(vehicle_core_notification_tests tests/notification_channel_tests.cpp)
  target_link_libraries(vehicle_core_notification_tests PRIVATE vehicle_core_notifications)
  target_link_libraries(vehicle_core_notification_tests PRIVATE Threads::Threads)
  target_compile_features(vehicle_core_notification_tests PRIVATE cxx_std_17)
  if(MSVC)
    target_compile_options(vehicle_core_notification_tests PRIVATE /W4 /permissive-)
  else()
    target_compile_options(vehicle_core_notification_tests PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME vehicle_core_notification_tests COMMAND vehicle_core_notification_tests)
endif()
