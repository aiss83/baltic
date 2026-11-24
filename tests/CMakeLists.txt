add_executable(test_baltic test_baltic.c)
target_link_libraries(test_baltic PRIVATE baltic Threads::Threads)
target_compile_definitions(test_baltic PRIVATE
    BALTIC_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(test_baltic PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME api COMMAND test_baltic)
set_tests_properties(api PROPERTIES TIMEOUT 180)

add_executable(test_egm2008 test_egm2008.c)
target_link_libraries(test_egm2008 PRIVATE baltic)
target_compile_definitions(test_egm2008 PRIVATE
    BALTIC_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(test_egm2008 PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME egm2008 COMMAND test_egm2008)
set_tests_properties(egm2008 PROPERTIES TIMEOUT 300 SKIP_RETURN_CODE 77)

if(BALTIC_BUILD_CLI AND UNIX)
    add_test(NAME cli
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/test_cli.sh"
                "$<TARGET_FILE:baltic_cli>"
                "${PROJECT_SOURCE_DIR}")
    set_tests_properties(cli PROPERTIES TIMEOUT 300 SKIP_RETURN_CODE 77)
endif()
