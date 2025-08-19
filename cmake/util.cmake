set(SYS_COMPILE_FLAGS 
    -Wall
    -Wextra
    -Werror
    -Wshadow
    -Wno-psabi
    -Wpedantic
    CACHE STRING "SYS_COMPILE_FLAGS"
)

function(ProjectTestSetup snippets)
    # Compile commands used by clangd
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON PARENT_SCOPE)
    set(CMAKE_CXX_STANDARD 20 PARENT_SCOPE)

    list(APPEND SNIPPET_ROOT $ENV{APP_ROOT})
    set(SNIPPET_ROOT ${SNIPPET_ROOT} PARENT_SCOPE)
    set(SNIPPET ${snippets} PARENT_SCOPE)

    list(APPEND ZEPHYR_EXTRA_MODULES 
        $ENV{APP_ROOT}/src
    )

    set(ZEPHYR_EXTRA_MODULES ${ZEPHYR_EXTRA_MODULES} PARENT_SCOPE)

    list(APPEND DTS_ROOT $ENV{APP_ROOT})
    set(DTS_ROOT ${DTS_ROOT} PARENT_SCOPE)

    include(MunkeiVersionFromGit)

    version_from_git(
    LOG       ON
    TIMESTAMP "%Y%m%d%H%M%S"
    )

    set( GIT_TAG       ${GIT_TAG}       PARENT_SCOPE )
    set( GIT_DESCRIBE  ${GIT_DESCRIBE}  PARENT_SCOPE )
    set( GIT_BRANCH    ${GIT_BRANCH}    PARENT_SCOPE )
    set( GIT_HASH      ${GIT_HASH}      PARENT_SCOPE )
    set( GIT_DIRTY     ${GIT_DIRTY}     PARENT_SCOPE )
    set( SEMVER        ${SEMVER}        PARENT_SCOPE )
    set( VERSION       ${VERSION}       PARENT_SCOPE )
    set( VERSION_MAJOR ${VERSION_MAJOR} PARENT_SCOPE )
    set( VERSION_MINOR ${VERSION_MINOR} PARENT_SCOPE )
    set( VERSION_PATCH ${VERSION_PATCH} PARENT_SCOPE )

    configure_file($ENV{APP_ROOT}/configuration/version.hpp.in version.hpp)
endfunction()

function(SetupTarget target_name is_unit_test)
    set_target_properties(zephyr_interface PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:zephyr_interface,INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_options(${target_name} PRIVATE ${SYS_COMPILE_FLAGS})
    
    target_include_directories(${target_name} PRIVATE 
        $ENV{APP_ROOT}/include
        $ENV{APP_ROOT}/drivers
        $ENV{APP_ROOT}/src
        $ENV{APP_ROOT}
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_BINARY_DIR}
        /usr/local/include
    )

    zephyr_include_directories(
        $ENV{APP_ROOT}/include
        $ENV{APP_ROOT}/src
        ${CMAKE_BINARY_DIR}
        /usr/local/include
    )

    list(APPEND CMAKE_MODULE_PATH ${ZEPHYR_BASE}/modules/nanopb)
    include(nanopb)

    zephyr_nanopb_sources(${target_name} $ENV{APP_ROOT}/proto/core.proto)
    zephyr_include_directories(${CMAKE_BINARY_DIR}/proto)

    if(is_unit_test)
        target_compile_options(app PRIVATE -DUNIT_TESTING)
        zephyr_compile_options(-DUNIT_TESTING)
    endif()

    # Prevent race conditions due to parallel jobs
    option(TWISTER "Using Twister" OFF)
    if(NOT TWISTER)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E rm -f $ENV{APP_ROOT}/compile_commands.json
            COMMAND yes | cp -f ${CMAKE_BINARY_DIR}/compile_commands.json $ENV{APP_ROOT}/compile_commands.json
            COMMENT "Moving compile_commands.json to $ENV{APP_ROOT}"
            VERBATIM
        )
    endif()
endfunction()
