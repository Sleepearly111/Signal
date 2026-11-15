cmake_minimum_required(VERSION 3.16)
project(filter_identification_app_reference LANGUAGES C)

if(NOT TARGET sweep_service OR
   NOT TARGET transfer_response OR
   NOT TARGET filter_classifier)
    message(FATAL_ERROR "Filter identification app dependencies are not enabled")
endif()

add_library(
    filter_identification_app
    STATIC
    src/filter_identification_app.c
)
target_include_directories(
    filter_identification_app
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(
    filter_identification_app
    PUBLIC
        sweep_service
        transfer_response
        filter_classifier
)
target_compile_features(filter_identification_app PUBLIC c_std_11)

if(MSVC)
    target_compile_options(filter_identification_app PRIVATE /W4 /WX)
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        filter_identification_app
        PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
endif()

if(SIGNAL_FRAMEWORK_BUILD_TESTS OR PROJECT_IS_TOP_LEVEL)
    enable_testing()
    add_executable(
        filter_identification_app_tests
        tests/test_filter_identification_app.c
    )
    target_link_libraries(
        filter_identification_app_tests
        PRIVATE filter_identification_app
    )
    target_compile_features(
        filter_identification_app_tests
        PRIVATE c_std_11
    )
    add_test(
        NAME filter_identification_app_tests
        COMMAND filter_identification_app_tests
    )
endif()
