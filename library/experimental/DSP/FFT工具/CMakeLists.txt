cmake_minimum_required(VERSION 3.16)

project(fft_tools LANGUAGES C)

add_library(fft_tools STATIC src/fft_tools.c)
target_include_directories(fft_tools PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(fft_tools PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(fft_tools PRIVATE -Wall -Wextra -Werror)
endif()
if(NOT MSVC)
    target_link_libraries(fft_tools PUBLIC m)
endif()

add_executable(test_fft_tools tests/test_fft_tools.c)
set_target_properties(test_fft_tools PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_fft_tools PRIVATE -Wall -Wextra -Werror)
endif()
target_link_libraries(test_fft_tools PRIVATE fft_tools)

enable_testing()
add_test(NAME fft_tools_tests COMMAND test_fft_tools)
