set(sensor_file
  "${PROJECT_SOURCE_DIR}/src/sensor/sc132gs_linear_1088x1280_raw10_30fps_1lane.c")
set(vio_file "${PROJECT_SOURCE_DIR}/src/vio_pipeline.cpp")

foreach(required_file IN ITEMS "${sensor_file}" "${vio_file}")
  if(NOT EXISTS "${required_file}")
    message(FATAL_ERROR "required synchronization source is missing: ${required_file}")
  endif()
endforeach()

file(READ "${sensor_file}" sensor)
file(READ "${vio_file}" vio)

if(NOT sensor MATCHES "#define SENSOR_FPS 30")
  message(FATAL_ERROR "SC132GS must run at 30 FPS")
endif()
if(NOT sensor MATCHES "\\.sensor_mode[ \t]*=[ \t]*SLAVE_M")
  message(FATAL_ERROR "SC132GS must use SLAVE_M")
endif()
if(NOT sensor MATCHES
   "\\.lpwm_attr[ \t\r\n]*=[ \t\r\n]*\\{[ \t\r\n]*\\.enable[ \t]*=[ \t]*1")
  message(FATAL_ERROR "SC132GS LPWM must be enabled")
endif()
if(vio MATCHES "lpwm_attr\\.enable[ \t]*=[ \t]*0")
  message(FATAL_ERROR "runtime code must not disable LPWM")
endif()
