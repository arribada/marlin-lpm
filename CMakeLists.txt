cmake_minimum_required(VERSION 3.28)

list(APPEND CMAKE_MODULE_PATH $ENV{APP_ROOT}/cmake)

include(util)

ProjectTestSetup("")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(LPM LANGUAGES C CXX ASM)

SetupTarget(app FALSE)
target_sources(app PRIVATE src/main.cpp src/events.cpp)
