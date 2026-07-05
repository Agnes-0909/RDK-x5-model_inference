cmake_minimum_required(VERSION 3.14)

project(mipi_real_time_proj LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

set(DNN_ROOT "${PROJECT_ROOT}/3rdlibrary/DNN" CACHE PATH
    "Path to the RDK DNN SDK root")
set(OpenCV_DIR "${PROJECT_ROOT}/3rdlibrary/opencv_aarch64/lib/cmake/opencv4" CACHE PATH
    "Path to the OpenCV CMake package directory")
set(RDK_CAMERA_ROOT "${PROJECT_ROOT}/3rdlibrary/RDK_CAMERA" CACHE PATH
    "Path to copied RDK camera SDK headers and libraries")

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs CONFIG)

if(NOT TARGET model_inference_base OR NOT TARGET yolo_infer)
  add_subdirectory("${PROJECT_ROOT}/model_infer"
                   "${CMAKE_CURRENT_BINARY_DIR}/model_infer_deps")
endif()

add_library(mipi_zero_copy_core STATIC
  src/mipi_zero_copy_app.cpp
)

target_include_directories(mipi_zero_copy_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_ROOT}/model_infer/yolo_infer
    ${PROJECT_ROOT}/model_infer/model_inference_base
    ${DNN_ROOT}/include
    ${OpenCV_INCLUDE_DIRS}
)

add_executable(test_mipi_zero_copy_config
  test/test_mipi_zero_copy_config.cpp
)

target_include_directories(test_mipi_zero_copy_config
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(test_mipi_zero_copy_config
  PRIVATE
    mipi_zero_copy_core
)

option(BUILD_MIPI_CAMERA_APP "Build the hardware MIPI camera inference executable" ON)

set(RDK_CAMERA_INCLUDE_DIRS "" CACHE STRING
    "Semicolon-separated include directories for RDK camera, ROS2, yaml-cpp and jsoncpp headers")
set(RDK_CAMERA_LIBRARY_DIRS "" CACHE STRING
    "Semicolon-separated library directories for RDK camera runtime libraries")
set(RDK_CAMERA_LIBRARIES
    "vpf;cam;hbmem;gdcbin;Nano2D;Nano2Dutil;pthread"
    CACHE STRING
    "Semicolon-separated RDK camera and support libraries")

if(BUILD_MIPI_CAMERA_APP)
  set(_camera_default_include_dirs
    ${RDK_CAMERA_ROOT}/include
    ${RDK_CAMERA_INCLUDE_DIRS}
    /usr/include
    /usr/local/include
    /opt/hobot/include
    /opt/tros/include
    $ENV{TROS_ROOT}/include
  )

  foreach(_required_header
      hbn_api.h
      hb_camera_interface.h
      vin_cfg.h
      hb_media_codec.h)
    find_file(_found_${_required_header}
      NAMES ${_required_header}
      PATHS ${_camera_default_include_dirs}
      NO_DEFAULT_PATH)
    if(NOT _found_${_required_header})
      message(FATAL_ERROR
        "Missing required camera SDK/ROS header '${_required_header}'. "
        "Pass -DRDK_CAMERA_INCLUDE_DIRS='/path/to/include;/opt/tros/<distro>/include'.")
    endif()
  endforeach()

  set(MIPI_CAMERA_SOURCES
    src/hobot_mipi_cap.cpp
    src/hobot_mipi_cap_iml.cpp
    src/hobot_mipi_factory.cpp
    src/hobot_mipi_calibration.cpp
    src/hobot_mipi_comm.cpp
    src/sensor/sc132gs_linear_1088x1280_raw10_30fps_1lane.c
    src/sensor/sc230ai_linear_1920x1080_raw10_30fps_1lane.c
    src/sensor/ov5647_linear_1920x1080_raw10_30fps_2lane.c
    src/sensor/imx219_linear_1920x1080_raw10_30fps_2lane.c
    src/sensor/imx477_linear_1920x1080_raw12_50fps_2lane.c
    src/sensor/imx415_linear_3840x2160_raw10_30fps_2lane.c
    src/sensor/vp_sensors.c
  )

  add_executable(mipi_zero_copy_infer
    src/mipi_zero_copy_infer.cpp
    ${MIPI_CAMERA_SOURCES}
  )

  target_compile_definitions(mipi_zero_copy_infer PRIVATE PLATFORM_X5)

  target_include_directories(mipi_zero_copy_infer
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/src/sensor
      ${RDK_CAMERA_ROOT}/include
      ${RDK_CAMERA_INCLUDE_DIRS}
      ${OpenCV_INCLUDE_DIRS}
      ${PROJECT_ROOT}/model_infer/yolo_infer
      ${PROJECT_ROOT}/model_infer/model_inference_base
      ${DNN_ROOT}/include
  )

  set(_rdk_camera_library_dirs ${RDK_CAMERA_ROOT}/lib ${RDK_CAMERA_LIBRARY_DIRS})
  if(_rdk_camera_library_dirs)
    target_link_directories(mipi_zero_copy_infer PRIVATE ${_rdk_camera_library_dirs})
    foreach(link_dir IN LISTS _rdk_camera_library_dirs)
      target_link_options(mipi_zero_copy_infer PRIVATE "-Wl,-rpath-link,${link_dir}")
    endforeach()
  endif()

  if(OpenCV_LIBRARY_DIRS)
    foreach(link_dir IN LISTS OpenCV_LIBRARY_DIRS)
      target_link_options(mipi_zero_copy_infer PRIVATE "-Wl,-rpath-link,${link_dir}")
    endforeach()
  endif()

  target_link_options(mipi_zero_copy_infer PRIVATE
    "-Wl,-rpath-link,${DNN_ROOT}/lib"
  )

  target_link_libraries(mipi_zero_copy_infer
    PRIVATE
      mipi_zero_copy_core
      yolo_infer
      ${OpenCV_LIBS}
      ${RDK_CAMERA_LIBRARIES}
  )
endif()
