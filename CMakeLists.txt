# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ESP32S3_Wristband_v3.0)

# Generate SPIFFS image from spiffs_data/ for the "storage" partition
spiffs_create_partition_image(storage ${CMAKE_CURRENT_SOURCE_DIR}/spiffs_data FLASH_IN_PROJECT)
