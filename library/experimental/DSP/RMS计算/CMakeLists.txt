cmake_minimum_required(VERSION 3.16)

project(rms_calc LANGUAGES C)

add_library(rms_calc STATIC
    src/rms_calc.c
)

target_include_directories(rms_calc
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_target_properties(rms_calc PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(rms_calc PRIVATE -Wall -Wextra -Werror)
endif()

if(NOT MSVC)
    target_link_libraries(rms_calc PUBLIC m)
endif()

add_executable(test_rms_calc
    tests/test_rms_calc.c
)

set_target_properties(test_rms_calc PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_rms_calc PRIVATE -Wall -Wextra -Werror)
endif()

target_link_libraries(test_rms_calc PRIVATE rms_calc)

enable_testing()
add_test(NAME rms_calc_tests COMMAND test_rms_calc)
