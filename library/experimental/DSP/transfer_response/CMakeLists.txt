cmake_minimum_required(VERSION 3.16)
project(transfer_response_module LANGUAGES C)

add_library(transfer_response STATIC src/transfer_response.c)
target_include_directories(transfer_response PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(transfer_response PUBLIC c_std_11)

if(NOT MSVC)
    target_link_libraries(transfer_response PUBLIC m)
endif()

if(MSVC)
    target_compile_options(transfer_response PRIVATE /W4 /WX)
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(transfer_response PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(SIGNAL_FRAMEWORK_BUILD_TESTS OR PROJECT_IS_TOP_LEVEL)
    enable_testing()
    add_executable(transfer_response_tests tests/test_transfer_response.c)
    target_link_libraries(transfer_response_tests PRIVATE transfer_response)
    target_compile_features(transfer_response_tests PRIVATE c_std_11)
    add_test(NAME transfer_response_tests COMMAND transfer_response_tests)
endif()
