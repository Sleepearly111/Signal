cmake_minimum_required(VERSION 3.16)

project(frequency_measure_core LANGUAGES C)

add_library(
    frequency_measure_core
    STATIC
    src/frequency_measure_core.c
)

target_include_directories(
    frequency_measure_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_target_properties(
    frequency_measure_core
    PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
)

if(MSVC)
    target_compile_options(frequency_measure_core PRIVATE /W4 /WX)
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        frequency_measure_core
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
    )
endif()

add_executable(
    test_frequency_measure_core
    tests/test_frequency_measure_core.c
)

set_target_properties(
    test_frequency_measure_core
    PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
)

if(MSVC)
    target_compile_options(test_frequency_measure_core PRIVATE /W4 /WX)
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_frequency_measure_core
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
    )
endif()

target_link_libraries(
    test_frequency_measure_core
    PRIVATE
        frequency_measure_core
)

enable_testing()
add_test(
    NAME DSP-INSTFREQ-001
    COMMAND test_frequency_measure_core
)
