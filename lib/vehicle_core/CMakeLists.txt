add_library(vehicle_core STATIC
  src/vehicle_core.cpp
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
