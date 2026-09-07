if(COMMAND idf_component_register)
  idf_component_register(INCLUDE_DIRS "include" REQUIRES vehicle_core)
  return()
endif()

# Stage 0 exposes Mazda-facing types without adding a production runtime
# target. Later stages can add sources without changing consumers' include
# paths or the root test graph.
add_library(mazda_contracts INTERFACE)
target_include_directories(mazda_contracts INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mazda_contracts INTERFACE vehicle_core)
