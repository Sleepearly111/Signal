cmake_minimum_required(VERSION 3.16)

project(ad9833_driver LANGUAGES C)

add_library(ad9833 STATIC src/ad9833.c)
target_include_directories(ad9833 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(ad9833 PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ad9833 PRIVATE -Wall -Wextra -Werror)
endif()
if(NOT MSVC)
    target_link_libraries(ad9833 PUBLIC m)
endif()

add_executable(test_ad9833 tests/test_ad9833.c)
set_target_properties(test_ad9833 PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_ad9833 PRIVATE -Wall -Wextra -Werror)
endif()
target_link_libraries(test_ad9833 PRIVATE ad9833)

enable_testing()
add_test(NAME ad9833_tests COMMAND test_ad9833)
