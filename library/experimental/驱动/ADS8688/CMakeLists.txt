cmake_minimum_required(VERSION 3.16)

project(ads8688_driver LANGUAGES C)

add_library(ads8688 STATIC src/ads8688.c)
target_include_directories(ads8688 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(ads8688 PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ads8688 PRIVATE -Wall -Wextra -Werror)
endif()

add_executable(test_ads8688 tests/test_ads8688.c)
set_target_properties(test_ads8688 PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_ads8688 PRIVATE -Wall -Wextra -Werror)
endif()
target_link_libraries(test_ads8688 PRIVATE ads8688)

enable_testing()
add_test(NAME ads8688_tests COMMAND test_ads8688)
