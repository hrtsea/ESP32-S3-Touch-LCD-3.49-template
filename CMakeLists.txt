# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Exclude audio components on IDF 5.2.x: esp_codec_dev pulls in the legacy
# driver/i2c.c, which registers a constructor that aborts when the new
# i2c_master driver is also linked. Skip them so app_main can run.
set(EXCLUDE_COMPONENTS audio_bsp codec_board espressif__esp_codec_dev)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(COMPILE_OPTIONS -Wno-error=stringop-truncation APPEND)
project(12_HelloWorld_Skeleton)

