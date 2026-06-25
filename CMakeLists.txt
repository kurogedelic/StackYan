# StackYan — ESP32 physical Tool Server for M5Stack StackChan/CoreS3.
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(StackYan LANGUAGES C CXX ASM)

idf_build_set_property(CMAKE_CXX_STANDARD 17)
idf_build_set_property(CMAKE_CXX_STANDARD_REQUIRED ON)
