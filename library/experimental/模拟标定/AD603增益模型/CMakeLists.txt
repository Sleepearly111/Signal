cmake_minimum_required(VERSION 3.16)

project(ad603_model LANGUAGES C)

get_filename_component(LUT_INTERP_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../DSP/一维查表插值"
    ABSOLUTE
)

add_library(ad603_lut_interp_dependency STATIC
    "${LUT_INTERP_ROOT}/src/lut_interp.c"
)

target_include_directories(ad603_lut_interp_dependency
    PUBLIC
        "${LUT_INTERP_ROOT}/include"
)

add_library(ad603_model STATIC
    src/ad603_model.c
)

target_include_directories(ad603_model
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        "${LUT_INTERP_ROOT}/include"
)

foreach(target ad603_lut_interp_dependency ad603_model)
    set_target_properties(${target} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
    )
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    endif()
endforeach()

if(NOT MSVC)
    target_link_libraries(ad603_lut_interp_dependency PUBLIC m)
    target_link_libraries(ad603_model PUBLIC m)
endif()

target_link_libraries(ad603_model PRIVATE ad603_lut_interp_dependency)

add_executable(test_ad603_model
    tests/test_ad603_model.c
)

set_target_properties(test_ad603_model PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_ad603_model PRIVATE -Wall -Wextra -Werror)
endif()

target_link_libraries(test_ad603_model PRIVATE ad603_model)

enable_testing()
add_test(NAME ad603_model_tests COMMAND test_ad603_model)
