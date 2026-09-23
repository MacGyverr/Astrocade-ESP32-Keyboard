cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

if(IDF_TARGET STREQUAL "esp32")
    idf_build_set_property(DEPENDENCIES_LOCK "${CMAKE_CURRENT_LIST_DIR}/dependencies.esp32.lock")
endif()

project(astrocade_keyboard_ble)
