cmake_minimum_required(VERSION 3.28)

list(APPEND CMAKE_MODULE_PATH $ENV{APP_ROOT}/cmake)

include(util)

ProjectTestSetup("testing")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(hello-world-test LANGUAGES C CXX ASM)

SetupTarget(app true)
target_sources(app PRIVATE src/main.cpp $ENV{APP_ROOT}/src/events.cpp)
target_include_directories(app PRIVATE $ENV{APP_ROOT}/src/supervisor)