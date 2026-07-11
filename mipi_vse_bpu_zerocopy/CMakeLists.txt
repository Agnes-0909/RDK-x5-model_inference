cmake_minimum_required(VERSION 3.14)
project(mipi_vse_bpu_zerocopy LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

add_library(mipi_zerocopy_core STATIC
  src/async_logger.cpp
  src/app_config.cpp
  src/bounded_frame_queue.cpp
  src/buffer_layout.cpp
  src/channel_worker.cpp
  src/detection_mapper.cpp
  src/dual_channel_coordinator.cpp
  src/cleanup_stack.cpp
  src/registered_buffer_cache.cpp
  src/tensor_binding.cpp
  src/vse_frame.cpp
  src/yolo11_postprocess.cpp
)
target_include_directories(mipi_zerocopy_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(test_app_config test/test_app_config.cpp)
target_include_directories(test_app_config PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_app_config PRIVATE mipi_zerocopy_core)
add_test(NAME test_app_config COMMAND test_app_config)

add_executable(test_async_logger test/test_async_logger.cpp)
target_link_libraries(test_async_logger PRIVATE mipi_zerocopy_core pthread)
add_test(NAME test_async_logger COMMAND test_async_logger)

add_executable(test_bounded_frame_queue test/test_bounded_frame_queue.cpp)
target_link_libraries(test_bounded_frame_queue PRIVATE mipi_zerocopy_core pthread)
add_test(NAME test_bounded_frame_queue COMMAND test_bounded_frame_queue)

add_executable(test_channel_worker test/test_channel_worker.cpp)
target_link_libraries(test_channel_worker PRIVATE mipi_zerocopy_core pthread)
add_test(NAME test_channel_worker COMMAND test_channel_worker)

add_executable(test_dual_channel_coordinator
  test/test_dual_channel_coordinator.cpp)
target_link_libraries(test_dual_channel_coordinator PRIVATE
  mipi_zerocopy_core pthread)
add_test(NAME test_dual_channel_coordinator COMMAND test_dual_channel_coordinator)

add_executable(test_geometry_and_layout test/test_geometry_and_layout.cpp)
target_link_libraries(test_geometry_and_layout PRIVATE mipi_zerocopy_core)
add_test(NAME test_geometry_and_layout COMMAND test_geometry_and_layout)

add_executable(test_resource_lifecycle test/test_resource_lifecycle.cpp)
target_link_libraries(test_resource_lifecycle PRIVATE mipi_zerocopy_core)
add_test(NAME test_resource_lifecycle COMMAND test_resource_lifecycle)

add_executable(test_yolo11_postprocess test/test_yolo11_postprocess.cpp)
target_link_libraries(test_yolo11_postprocess PRIVATE mipi_zerocopy_core)
add_test(NAME test_yolo11_postprocess COMMAND test_yolo11_postprocess)

add_executable(test_tensor_binding test/test_tensor_binding.cpp)
target_link_libraries(test_tensor_binding PRIVATE mipi_zerocopy_core)
add_test(NAME test_tensor_binding COMMAND test_tensor_binding)

add_executable(test_vio_state_machine test/test_vio_state_machine.cpp)
target_link_libraries(test_vio_state_machine PRIVATE mipi_zerocopy_core)
add_test(NAME test_vio_state_machine COMMAND test_vio_state_machine)

add_test(NAME no_image_copy
  COMMAND ${CMAKE_COMMAND}
    -DPROJECT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/test/check_no_image_copy.cmake)

option(BUILD_HARDWARE_APP "Build the RDK X5 hardware executable" ON)

set(DNN_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../3rdlibrary/DNN" CACHE PATH
    "Path to the RDK DNN SDK")
set(RDK_CAMERA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../3rdlibrary/RDK_CAMERA" CACHE PATH
    "Path to the RDK Camera SDK")

if(BUILD_HARDWARE_APP)
  add_library(mipi_zerocopy_dnn STATIC
    src/zero_copy_channel_processor.cpp
    src/zero_copy_yolo_runner.cpp
    src/vio_pipeline.cpp
    src/sensor/sc132gs_linear_1088x1280_raw10_30fps_1lane.c
  )
  target_include_directories(mipi_zerocopy_dnn
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src/sensor
      ${DNN_ROOT}/include
      ${RDK_CAMERA_ROOT}/include)
  target_link_directories(mipi_zerocopy_dnn PRIVATE
    ${DNN_ROOT}/lib ${RDK_CAMERA_ROOT}/lib)
  target_link_options(mipi_zerocopy_dnn PRIVATE
    "-Wl,-rpath-link,${DNN_ROOT}/lib"
    "-Wl,-rpath-link,${RDK_CAMERA_ROOT}/lib")
  target_link_libraries(mipi_zerocopy_dnn PRIVATE
    mipi_zerocopy_core dnn vpf cam hbmem pthread)

  add_executable(mipi_vse_bpu_zerocopy src/main.cpp)
  target_include_directories(mipi_vse_bpu_zerocopy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${DNN_ROOT}/include
    ${RDK_CAMERA_ROOT}/include)
  target_link_directories(mipi_vse_bpu_zerocopy PRIVATE
    ${DNN_ROOT}/lib ${RDK_CAMERA_ROOT}/lib)
  target_link_options(mipi_vse_bpu_zerocopy PRIVATE
    "-Wl,-rpath-link,${DNN_ROOT}/lib"
    "-Wl,-rpath-link,${RDK_CAMERA_ROOT}/lib")
  target_link_libraries(mipi_vse_bpu_zerocopy PRIVATE
    mipi_zerocopy_dnn mipi_zerocopy_core dnn vpf cam hbmem pthread)
endif()
