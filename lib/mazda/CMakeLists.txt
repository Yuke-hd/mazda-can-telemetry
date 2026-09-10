if(COMMAND idf_component_register)
  idf_component_register(SRCS "src/state.cpp" "src/mazda_candidate.cpp"
                         INCLUDE_DIRS "include" PRIV_INCLUDE_DIRS "internal_include"
                         REQUIRES vehicle_core)
  return()
endif()

add_library(mazda STATIC
  src/state.cpp
  src/mazda_candidate.cpp
)
# The public include root contains only application-facing Mazda contracts.
# Decoder/service/lighting handoffs are implementation-only and are granted to
# this production target through a PRIVATE include path.
target_include_directories(mazda
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/internal_include
)
target_link_libraries(mazda PUBLIC vehicle_core)
target_compile_features(mazda PUBLIC cxx_std_17)

if(MSVC)
  target_compile_options(mazda PRIVATE /W4 /permissive-)
else()
  target_compile_options(mazda PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Keep the Stage 0 target name as a source/build compatibility seam for the
# facade while the Mazda implementation now has an owned production target.
add_library(mazda_contracts INTERFACE)
target_include_directories(mazda_contracts INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mazda_contracts INTERFACE mazda)
