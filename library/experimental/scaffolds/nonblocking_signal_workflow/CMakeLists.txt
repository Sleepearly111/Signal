cmake_minimum_required(VERSION 3.16)

project(signal_workflow_scaffold LANGUAGES C)

add_library(signal_workflow_scaffold STATIC
    src/signal_workflow.c
)

target_include_directories(signal_workflow_scaffold
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_target_properties(signal_workflow_scaffold PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)

if(MSVC)
    target_compile_options(signal_workflow_scaffold PRIVATE /W4 /WX)
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        signal_workflow_scaffold
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
    )
endif()

add_executable(test_signal_workflow
    tests/test_signal_workflow.c
)

set_target_properties(test_signal_workflow PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)

if(MSVC)
    target_compile_options(test_signal_workflow PRIVATE /W4 /WX)
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_signal_workflow
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
    )
endif()

target_link_libraries(test_signal_workflow PRIVATE signal_workflow_scaffold)

enable_testing()
add_test(NAME signal_workflow_scaffold_tests COMMAND test_signal_workflow)
