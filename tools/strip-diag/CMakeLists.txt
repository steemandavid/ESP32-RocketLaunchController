cmake_minimum_required(VERSION 3.16)
# Use the RLC firmware's own led_strip component, so this diagnostic exercises
# byte-for-byte the same driver as the real thing.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../managed_components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(strip-diag)
