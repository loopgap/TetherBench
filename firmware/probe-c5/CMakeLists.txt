cmake_minimum_required(VERSION 3.16)
file(STRINGS ../../VERSION PROJECT_VER LIMIT_COUNT 1)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tetherbench_probe_c5)
