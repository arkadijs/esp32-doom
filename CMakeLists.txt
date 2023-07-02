cmake_minimum_required(VERSION 3.16)

set(COMPONENTS esptool_py main prboom-esp32-compat prboom)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(doom)
