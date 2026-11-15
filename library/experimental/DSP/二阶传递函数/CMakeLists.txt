cmake_minimum_required(VERSION 3.16)

project(tf2 LANGUAGES C)

add_library(tf2 STATIC
    src/tf2.c
)

target_include_directories(tf2
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_target_properties(tf2 PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(tf2 PRIVATE -Wall -Wextra -Werror)
endif()

if(NOT MSVC)
    target_link_libraries(tf2 PUBLIC m)
endif()

add_executable(test_tf2
    tests/test_tf2.c
)

set_target_properties(test_tf2 PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_tf2 PRIVATE -Wall -Wextra -Werror)
endif()

target_link_libraries(test_tf2 PRIVATE tf2)

enable_testing()
add_test(NAME tf2_response_tests COMMAND test_tf2)
