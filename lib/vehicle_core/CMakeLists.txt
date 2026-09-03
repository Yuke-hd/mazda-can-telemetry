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
