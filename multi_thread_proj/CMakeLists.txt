cmake_minimum_required(VERSION 3.14)

project(multi_thread_proj LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

set(DNN_ROOT "${PROJECT_ROOT}/3rdlibrary/DNN" CACHE PATH
    "Path to the RDK DNN SDK root")
set(OpenCV_DIR "${PROJECT_ROOT}/3rdlibrary/opencv_aarch64/lib/cmake/opencv4" CACHE PATH
    "Path to the OpenCV CMake package directory")

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs CONFIG)

if(NOT TARGET model_inference_base OR NOT TARGET yolo_infer)
  add_subdirectory("${PROJECT_ROOT}/model_infer"
                   "${CMAKE_CURRENT_BINARY_DIR}/model_infer_deps")
endif()

add_library(mt_common STATIC
  src/common/logger.cpp
  src/common/protocol.cpp
  src/common/shared_memory_queue.cpp
  src/common/image_codec.cpp
  src/common/uds_socket.cpp
)

target_include_directories(mt_common
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(mt_common
  PUBLIC
    ${OpenCV_LIBS}
)

add_executable(test_protocol
  test/test_protocol.cpp
)
target_link_libraries(test_protocol PRIVATE mt_common)

add_executable(test_shared_memory_queue
  test/test_shared_memory_queue.cpp
)
target_link_libraries(test_shared_memory_queue PRIVATE mt_common)

add_executable(test_result_aggregator
  test/test_result_aggregator.cpp
  src/server/result_aggregator.cpp
)
target_include_directories(test_result_aggregator
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server
)
target_link_libraries(test_result_aggregator PRIVATE mt_common)

add_executable(test_manager_result_merge
  test/test_manager_result_merge.cpp
  src/manager/manager_app.cpp
)
target_include_directories(test_manager_result_merge
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/manager
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server
)
target_link_libraries(test_manager_result_merge PRIVATE mt_common)

add_executable(test_logger
  test/test_logger.cpp
)
target_link_libraries(test_logger PRIVATE mt_common)

add_executable(test_image_codec_transport
  test/test_image_codec_transport.cpp
)
target_link_libraries(test_image_codec_transport PRIVATE mt_common)

add_executable(test_server_args
  test/test_server_args.cpp
  src/server/server_app.cpp
  src/server/worker.cpp
  src/server/result_aggregator.cpp
)
target_include_directories(test_server_args
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server
    ${PROJECT_ROOT}/model_infer/yolo_infer
    ${PROJECT_ROOT}/model_infer/model_inference_base
    ${DNN_ROOT}/include
)
target_link_libraries(test_server_args
  PRIVATE
    mt_common
    yolo_infer
    model_inference_base
)

add_executable(mt_manager
  src/manager/manager_main.cpp
  src/manager/manager_app.cpp
)
target_link_libraries(mt_manager PRIVATE mt_common)

target_include_directories(mt_manager
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/manager
)

add_executable(mt_server
  src/server/server_main.cpp
  src/server/server_app.cpp
  src/server/worker.cpp
  src/server/result_aggregator.cpp
)
target_include_directories(mt_server
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server
    ${PROJECT_ROOT}/model_infer/yolo_infer
    ${PROJECT_ROOT}/model_infer/model_inference_base
    ${DNN_ROOT}/include
)
target_link_libraries(mt_server
  PRIVATE
    mt_common
    yolo_infer
    model_inference_base
)
