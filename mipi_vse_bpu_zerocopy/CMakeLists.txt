cmake_minimum_required(VERSION 3.14)
project(mipi_vse_bpu_zerocopy LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

add_library(mipi_zerocopy_core STATIC
  src/app_config.cpp
  src/buffer_layout.cpp
  src/detection_mapper.cpp
  src/registered_buffer_cache.cpp
  src/vse_frame.cpp
)
target_include_directories(mipi_zerocopy_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(test_app_config test/test_app_config.cpp)
target_include_directories(test_app_config PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_app_config PRIVATE mipi_zerocopy_core)
add_test(NAME test_app_config COMMAND test_app_config)

add_executable(test_geometry_and_layout test/test_geometry_and_layout.cpp)
target_link_libraries(test_geometry_and_layout PRIVATE mipi_zerocopy_core)
add_test(NAME test_geometry_and_layout COMMAND test_geometry_and_layout)

add_executable(test_resource_lifecycle test/test_resource_lifecycle.cpp)
target_link_libraries(test_resource_lifecycle PRIVATE mipi_zerocopy_core)
add_test(NAME test_resource_lifecycle COMMAND test_resource_lifecycle)

option(BUILD_HARDWARE_APP "Build the RDK X5 hardware executable" ON)
