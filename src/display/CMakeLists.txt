FILE(GLOB app_sources *.cpp)
zephyr_library()
zephyr_library_compile_options(${SYS_COMPILE_FLAGS})
zephyr_library_sources(${app_sources})