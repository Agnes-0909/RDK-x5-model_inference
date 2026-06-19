cmake_minimum_required(VERSION 3.14)

project(model_infer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

set(DNN_ROOT "${PROJECT_ROOT}/3rdlibrary/DNN" CACHE PATH
    "Path to the RDK DNN SDK root")
set(OpenCV_DIR "${PROJECT_ROOT}/3rdlibrary/opencv_aarch64/lib/cmake/opencv4" CACHE PATH
    "Path to the OpenCV CMake package directory")

if(NOT EXISTS "${DNN_ROOT}/include/dnn/hb_dnn.h")
  message(FATAL_ERROR
          "DNN headers not found under DNN_ROOT='${DNN_ROOT}'.")
endif()

if(NOT EXISTS "${DNN_ROOT}/lib/libdnn.so")
  message(FATAL_ERROR
          "libdnn.so not found under DNN_ROOT='${DNN_ROOT}'.")
endif()

if(NOT EXISTS "${OpenCV_DIR}/OpenCVConfig.cmake")
  message(FATAL_ERROR
          "OpenCVConfig.cmake not found under OpenCV_DIR='${OpenCV_DIR}'.")
endif()

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs CONFIG)

add_library(rdk_dnn SHARED IMPORTED GLOBAL)
set_target_properties(rdk_dnn PROPERTIES
  IMPORTED_LOCATION "${DNN_ROOT}/lib/libdnn.so"
  INTERFACE_INCLUDE_DIRECTORIES "${DNN_ROOT}/include"
)

add_library(model_inference_base STATIC
  model_inference_base/model_inference_base.cpp
)

target_include_directories(model_inference_base
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/model_inference_base
)

target_link_libraries(model_inference_base
  PUBLIC
    rdk_dnn
)

add_library(yolo_infer STATIC
  yolo_infer/yolo_infer.cpp
)

target_include_directories(yolo_infer
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/yolo_infer
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(yolo_infer
  PUBLIC
    model_inference_base
    ${OpenCV_LIBS}
)

add_executable(test_yolo_infer
  test/test_yolo_infer.cpp
)

target_link_libraries(test_yolo_infer
  PRIVATE
    yolo_infer
)

if(OpenCV_LIBRARY_DIRS)
  foreach(link_dir IN LISTS OpenCV_LIBRARY_DIRS)
    target_link_options(test_yolo_infer PRIVATE "-Wl,-rpath-link,${link_dir}")
  endforeach()
endif()

target_link_options(test_yolo_infer PRIVATE
  "-Wl,-rpath-link,${DNN_ROOT}/lib"
)
