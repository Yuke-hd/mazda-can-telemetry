if(COMMAND idf_component_register)
  idf_component_register(SRCS "src/raw_capture.cpp" INCLUDE_DIRS "include" REQUIRES vehicle_core)
  return()
endif()

add_library(raw_capture STATIC src/raw_capture.cpp)
target_include_directories(raw_capture PUBLIC include)
target_link_libraries(raw_capture PUBLIC vehicle_core)
target_compile_features(raw_capture PUBLIC cxx_std_17)
