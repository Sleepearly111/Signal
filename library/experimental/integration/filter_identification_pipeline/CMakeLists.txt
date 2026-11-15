cmake_minimum_required(VERSION 3.16)
project(filter_identification_pipeline_test LANGUAGES C)

if(NOT TARGET sweep_service OR
   NOT TARGET transfer_response OR
   NOT TARGET filter_classifier)
    message(FATAL_ERROR "Filter ID pipeline dependencies are not enabled")
endif()

add_executable(
    filter_identification_pipeline_tests
    tests/test_filter_identification_pipeline.c
)
target_link_libraries(
    filter_identification_pipeline_tests
    PRIVATE
        sweep_service
        transfer_response
        filter_classifier
)
target_compile_features(filter_identification_pipeline_tests PRIVATE c_std_11)

if(MSVC)
    target_compile_options(filter_identification_pipeline_tests PRIVATE /W4 /WX)
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        filter_identification_pipeline_tests
        PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
endif()

enable_testing()
add_test(
    NAME filter_identification_pipeline_tests
    COMMAND filter_identification_pipeline_tests
)
