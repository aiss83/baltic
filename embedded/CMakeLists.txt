# Standalone build of the embedded library.
#
#   cmake -S embedded -B build-embedded
#   cmake --build build-embedded
#   ctest --test-dir build-embedded --output-on-failure
#
# This directory can also be added with add_subdirectory(embedded) from the
# main project; in that case it does not define a new project or toolchain.

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.15)
    project(baltic_embedded C)
    set(CMAKE_C_STANDARD 99)
    set(CMAKE_C_STANDARD_REQUIRED ON)
endif()

option(BALTIC_EMBEDDED_BUILD_TESTS "Build embedded library tests" ON)

set(BALTIC_EMBEDDED_OPT_FLAGS "-Os"
    CACHE STRING "Optimisation flags for baltic_embedded (e.g. -Os or -O2)")

add_library(baltic_embedded STATIC
    src/baltic_embedded.c
)
add_library(Baltic::Embedded ALIAS baltic_embedded)

target_include_directories(baltic_embedded
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

set_target_properties(baltic_embedded PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    separate_arguments(BALTIC_EMBEDDED_OPT_FLAGS_LIST NATIVE_COMMAND
        "${BALTIC_EMBEDDED_OPT_FLAGS}")
    target_compile_options(baltic_embedded PRIVATE
        ${BALTIC_EMBEDDED_OPT_FLAGS_LIST}
        -ffunction-sections
        -fdata-sections
        -ffreestanding
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

if(BALTIC_EMBEDDED_BUILD_TESTS)
    enable_testing()
    add_executable(test_baltic_embedded
        tests/test_embedded.c
        grids/egm2008_moscow_2deg.c
        grids/egm2008_spb_2deg.c
    )
    target_link_libraries(test_baltic_embedded PRIVATE baltic_embedded)
    target_include_directories(test_baltic_embedded PRIVATE grids)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(test_baltic_embedded PRIVATE
            -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME embedded COMMAND test_baltic_embedded)
    set_tests_properties(embedded PROPERTIES TIMEOUT 60)
endif()
