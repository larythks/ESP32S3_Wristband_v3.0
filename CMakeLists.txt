# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# 添加额外的组件目录（按开发计划的目录结构）
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/components/drivers/common
    ${CMAKE_CURRENT_SOURCE_DIR}/components/drivers/mpu6050
    ${CMAKE_CURRENT_SOURCE_DIR}/components/drivers/sh1106
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ESP32S3_Wristband_v3.0)
