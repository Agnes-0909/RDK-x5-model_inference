set(main_file "${PROJECT_SOURCE_DIR}/src/main.cpp")
if(NOT EXISTS "${main_file}")
  message(FATAL_ERROR "required application source is missing: ${main_file}")
endif()

set(inference_sources
  "${PROJECT_SOURCE_DIR}/src/main.cpp"
  "${PROJECT_SOURCE_DIR}/src/vio_pipeline.cpp"
  "${PROJECT_SOURCE_DIR}/src/zero_copy_yolo_runner.cpp")

foreach(source IN LISTS inference_sources)
  file(READ "${source}" contents)
  if(contents MATCHES "hbSysWriteMem[ \\t\\r\\n]*\\(")
    message(FATAL_ERROR "image copy API hbSysWriteMem found in ${source}")
  endif()
  if(contents MATCHES "cv::resize[ \\t\\r\\n]*\\(")
    message(FATAL_ERROR "CPU resize found in ${source}")
  endif()
  if(contents MATCHES "memcpy[ \\t\\r\\n]*\\(")
    message(FATAL_ERROR "memcpy found in inference path ${source}")
  endif()
endforeach()
