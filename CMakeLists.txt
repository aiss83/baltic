cmake_minimum_required(VERSION 3.20)

project(baltic
    VERSION 0.1.0
    DESCRIPTION "Ellipsoidal to Baltic height conversion with EGM2008 and PROJ"
    LANGUAGES C CXX)

include(GNUInstallDirs)

option(BALTIC_BUNDLED_PROJ
       "Build the vendored PROJ source tree (third_party/proj)" ON)
option(BALTIC_BUILD_CLI "Build the baltic command line utility" ON)
option(BALTIC_BUILD_TESTS "Build the test suite" ON)
option(BALTIC_BUILD_EMBEDDED "Build the integer-only embedded library" ON)
option(BALTIC_BUILD_GENERATOR
       "Build the host EGM2008 -> C grid generator" ON)
option(BALTIC_INSTALL "Generate install rules" ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Threads REQUIRED)

set(BALTIC_PROJ_TARGET "")

if(BALTIC_BUNDLED_PROJ)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/proj/CMakeLists.txt")
        message(FATAL_ERROR
            "third_party/proj is missing. Run scripts/vendor-proj.sh or set "
            "BALTIC_BUNDLED_PROJ=OFF to use a system PROJ.")
    endif()

    # Keep the bundled build lean: no PROJ command line tools, tests,
    # networking or curl.  GeoTIFF support is required for EGM2008 .tif
    # grids and is enabled through libtiff.
    set(BUILD_APPS OFF CACHE BOOL "" FORCE)
    set(BUILD_PROJSYNC OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(ENABLE_CURL OFF CACHE BOOL "" FORCE)
    set(ENABLE_TIFF ON CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)

    add_subdirectory(third_party/proj EXCLUDE_FROM_ALL)

    set(BALTIC_PROJ_TARGET proj)
    message(STATUS "baltic: using bundled PROJ from third_party/proj")
else()
    find_package(PROJ 9 CONFIG QUIET)
    if(PROJ_FOUND)
        set(BALTIC_PROJ_TARGET PROJ::proj)
        message(STATUS "baltic: using system PROJ ${PROJ_VERSION}")
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(PROJ REQUIRED IMPORTED_TARGET proj>=9)
        set(BALTIC_PROJ_TARGET PkgConfig::PROJ)
        message(STATUS "baltic: using system PROJ ${PROJ_VERSION} (pkg-config)")
    endif()
endif()

add_library(baltic SHARED
    src/baltic.c
    src/baltic_internal.h
)
add_library(Baltic::baltic ALIAS baltic)

target_include_directories(baltic
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(baltic PRIVATE ${BALTIC_PROJ_TARGET} Threads::Threads)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(baltic PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BALTIC_BUILD_CLI)
    add_executable(baltic_cli tools/baltic.c)
    set_target_properties(baltic_cli PROPERTIES OUTPUT_NAME baltic)
    target_link_libraries(baltic_cli PRIVATE baltic)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(baltic_cli PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

if(BALTIC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(BALTIC_BUILD_EMBEDDED)
    if(NOT DEFINED BALTIC_EMBEDDED_BUILD_TESTS)
        set(BALTIC_EMBEDDED_BUILD_TESTS ${BALTIC_BUILD_TESTS} CACHE BOOL
            "Build embedded library tests")
    endif()
    add_subdirectory(embedded)
endif()

if(BALTIC_BUILD_GENERATOR AND NOT CMAKE_CROSSCOMPILING)
    add_executable(egm2008_to_c embedded/tools/egm2008_to_c.c)
    target_link_libraries(egm2008_to_c PRIVATE baltic)
    if(UNIX)
        target_link_libraries(egm2008_to_c PRIVATE m)
    endif()
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(egm2008_to_c PRIVATE
            -Wall -Wextra -Wpedantic)
    endif()
endif()

# Make an installed tree relocatable: libproj is installed next to
# libbaltic when BALTIC_BUNDLED_PROJ=ON.
if(APPLE)
    set_target_properties(baltic PROPERTIES INSTALL_RPATH "@loader_path")
    if(BALTIC_BUILD_CLI)
        set_target_properties(baltic_cli PROPERTIES
            INSTALL_RPATH "@loader_path/../lib")
    endif()
elseif(UNIX)
    set_target_properties(baltic PROPERTIES INSTALL_RPATH "\$ORIGIN")
    if(BALTIC_BUILD_CLI)
        set_target_properties(baltic_cli PROPERTIES
            INSTALL_RPATH "\$ORIGIN/../lib")
    endif()
endif()

if(BALTIC_INSTALL)
    if(BALTIC_BUNDLED_PROJ)
        # PROJ is built with EXCLUDE_FROM_ALL so that its own install rules
        # (which hard-code an absolute data path) do not run.  Install only
        # the runtime library needed by libbaltic.
        install(TARGETS proj
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()
    install(TARGETS baltic
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
    if(BALTIC_BUILD_CLI)
        install(TARGETS baltic_cli
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
    install(DIRECTORY include/baltic
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(FILES README.md LICENSE NOTICE
        DESTINATION ${CMAKE_INSTALL_DOCDIR})
    install(DIRECTORY docs/
        DESTINATION ${CMAKE_INSTALL_DOCDIR}
        FILES_MATCHING PATTERN "*.md")
    install(FILES data/README.md
        DESTINATION ${CMAKE_INSTALL_DOCDIR}/data)

    configure_file(cmake/baltic.pc.in
        "${CMAKE_CURRENT_BINARY_DIR}/baltic.pc" @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/baltic.pc"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
endif()
