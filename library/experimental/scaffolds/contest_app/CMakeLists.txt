cmake_minimum_required(VERSION 3.16)

project(contest_app_scaffold LANGUAGES C)

add_library(contest_app_scaffold STATIC src/contest_app.c)
target_include_directories(
    contest_app_scaffold
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
set_target_properties(contest_app_scaffold PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)

add_executable(test_contest_app tests/test_contest_app.c)
set_target_properties(test_contest_app PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)
target_link_libraries(test_contest_app PRIVATE contest_app_scaffold)

foreach(target contest_app_scaffold test_contest_app)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(
            ${target}
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Werror
        )
    endif()
endforeach()

enable_testing()
add_test(NAME contest_app_scaffold_tests COMMAND test_contest_app)
