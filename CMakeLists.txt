cmake_minimum_required(VERSION 3.22.1)

# 原生 ESP-IDF 固件工程入口；Arduino fallback 已移除。
option(SMS_MODEM_PASSTHROUGH "Build UART-only modem diagnostic firmware" OFF)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# 固件镜像描述与网页共用版本来源，避免 OTA 包内版本落后于网页显示。
file(READ "${CMAKE_CURRENT_LIST_DIR}/components/idf_config/include/idf_config.h" SMS_CONFIG_HEADER)
string(REGEX MATCH "IDF_FW_VERSION = \"([^\"]+)\"" SMS_VERSION_MATCH "${SMS_CONFIG_HEADER}")
if(NOT SMS_VERSION_MATCH)
  message(FATAL_ERROR "IDF_FW_VERSION not found")
endif()
set(PROJECT_VER "${CMAKE_MATCH_1}")
if(SMS_MODEM_PASSTHROUGH)
  string(APPEND PROJECT_VER "-uartdiag")
endif()
project(sms_forwarding_idf)
