cmake_minimum_required(VERSION 3.16)

project(beat_fll_controller LANGUAGES C)

add_library(beat_fll_controller STATIC src/beat_fll_controller.c)
target_include_directories(
    beat_fll_controller
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
set_target_properties(
    beat_fll_controller
    PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO
)

if(MSVC)
    target_compile_options(beat_fll_controller PRIVATE /W4 /WX)
else()
    target_compile_options(
        beat_fll_controller PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
endif()

add_executable(
    test_beat_fll_controller
    tests/test_beat_fll_controller.c
)
target_link_libraries(test_beat_fll_controller PRIVATE beat_fll_controller)
set_target_properties(
    test_beat_fll_controller
    PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO
)
if(MSVC)
    target_compile_options(test_beat_fll_controller PRIVATE /W4 /WX)
else()
    target_compile_options(
        test_beat_fll_controller PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
endif()

enable_testing()
add_test(NAME DSP-BEATFLL-001 COMMAND test_beat_fll_controller)
