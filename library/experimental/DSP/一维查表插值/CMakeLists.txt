cmake_minimum_required(VERSION 3.16)

project(lut_interp LANGUAGES C)

add_library(lut_interp STATIC
    src/lut_interp.c
)

target_include_directories(lut_interp
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_target_properties(lut_interp PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(lut_interp PRIVATE -Wall -Wextra -Werror)
endif()

if(NOT MSVC)
    target_link_libraries(lut_interp PUBLIC m)
endif()

add_executable(test_lut_interp
    tests/test_lut_interp.c
)

set_target_properties(test_lut_interp PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_lut_interp PRIVATE -Wall -Wextra -Werror)
endif()

target_link_libraries(test_lut_interp PRIVATE lut_interp)

enable_testing()
add_test(NAME lut_interp_tests COMMAND test_lut_interp)
