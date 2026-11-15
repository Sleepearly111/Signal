cmake_minimum_required(VERSION 3.16)
project(regulated_signal_output_app_reference LANGUAGES C)

if(NOT TARGET signal_workflow_scaffold)
    message(FATAL_ERROR "Regulated output app requires signal_workflow_scaffold")
endif()

add_library(
    regulated_signal_output_app
    STATIC
    src/regulated_signal_output_app.c
)
target_include_directories(
    regulated_signal_output_app
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(
    regulated_signal_output_app
    PUBLIC
        signal_workflow_scaffold
)
target_compile_features(regulated_signal_output_app PUBLIC c_std_11)

if(MSVC)
    target_compile_options(regulated_signal_output_app PRIVATE /W4 /WX)
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        regulated_signal_output_app
        PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
endif()

if(SIGNAL_FRAMEWORK_BUILD_TESTS OR PROJECT_IS_TOP_LEVEL)
    enable_testing()
    add_executable(
        regulated_signal_output_app_tests
        tests/test_regulated_signal_output_app.c
    )
    target_link_libraries(
        regulated_signal_output_app_tests
        PRIVATE regulated_signal_output_app
    )
    target_compile_features(
        regulated_signal_output_app_tests
        PRIVATE c_std_11
    )
    if(MSVC)
        target_compile_options(
            regulated_signal_output_app_tests
            PRIVATE /W4 /WX
        )
    elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(
            regulated_signal_output_app_tests
            PRIVATE -Wall -Wextra -Wpedantic -Werror
        )
    endif()
    add_test(
        NAME regulated_signal_output_app_tests
        COMMAND regulated_signal_output_app_tests
    )
endif()
