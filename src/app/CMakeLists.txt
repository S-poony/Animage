set(CMAKE_AUTOMOC ON)

add_executable(animage_m0_latency
  latency/main.cpp
  latency/latency_canvas.cpp
  latency/latency_canvas.h
)

target_link_libraries(animage_m0_latency PRIVATE Qt6::Widgets)
target_include_directories(animage_m0_latency PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/latency)

# A console window on Windows: the device listing and the report on exit are
# part of what this tool is for.
set_target_properties(animage_m0_latency PROPERTIES WIN32_EXECUTABLE OFF)
