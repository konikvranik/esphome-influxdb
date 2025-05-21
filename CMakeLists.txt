cmake_minimum_required(VERSION 3.16.0)
project(influxdb2)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

message(STATUS "IDF_DIR: $ENV{IDF_DIR}")
message(STATUS "ESPHOME_DIR: $ENV{ESPHOME_DIR}")

# Include ESPHome directories (adjust these paths to match your system)
include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/
        # ESP-IDF includes
        $ENV{IDF_DIR}/components/
        $ENV{IDF_DIR}/components/freertos/FreeRTOS-Kernel/include
        $ENV{IDF_DIR}/components/esp_common/include
        $ENV{IDF_DIR}/components/esp_system/include
        $ENV{IDF_DIR}/components/driver/include
        # ESPHome core, if available
        $ENV{ESPHOME_DIR}
)

# Add compiler definitions that ESPHome uses
add_definitions(
        -DUSE_ESP32
        -DUSE_ESP_IDF
        -DUSE_ESP32_FRAMEWORK_ESP_IDF
)

# Add your custom components as "libraries" for code completion purposes
add_library(influxdb2 STATIC
        components/influxdb2/influxdb_writer.cpp
)
