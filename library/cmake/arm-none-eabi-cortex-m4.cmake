# Generic Cortex-M4 cross-compilation settings for Signal Framework modules.
# Pass CMAKE_C_COMPILER on the command line; do not hard-code a user install path.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_FLAGS_INIT "-mcpu=cortex-m4 -mthumb")
set(CMAKE_EXE_LINKER_FLAGS_INIT "--specs=nosys.specs")
set(CMAKE_EXECUTABLE_SUFFIX ".elf")
