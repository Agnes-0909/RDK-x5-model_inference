cmake_minimum_required(VERSION 3.14)

project(model_inference_base LANGUAGES CXX)

add_library(model_inference_base
  model_inference_base.cpp
)

target_compile_features(model_inference_base PUBLIC cxx_std_17)

target_include_directories(model_inference_base
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../3rdlibrary/DNN/include
)

target_link_libraries(model_inference_base
  PUBLIC
    dnn
)
