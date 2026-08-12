add_library(raw_capture STATIC
  src/capture_reader.cpp
  src/capture_writer.cpp
  src/replay.cpp
)
target_include_directories(raw_capture PUBLIC include)
target_link_libraries(raw_capture PUBLIC vehicle_core)
target_compile_features(raw_capture PUBLIC cxx_std_17)
if(MSVC)
  target_compile_options(raw_capture PRIVATE /W4 /permissive-)
else()
  target_compile_options(raw_capture PRIVATE -Wall -Wextra -Wpedantic)
endif()
