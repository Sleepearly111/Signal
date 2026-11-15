cmake_minimum_required(VERSION 3.22)
project(g2026_host_tests LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
enable_testing()

function(g2026_configure_host_test target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
  endif()
endfunction()

add_executable(test_fpga_mock
    test_fpga_mock.c
    ../User/Adapter/fpga_mock_adapter.c
)
target_include_directories(test_fpga_mock PRIVATE
    ../User/Adapter
    ../User/Service
    ../User/Config
)
if(NOT MSVC)
  target_link_libraries(test_fpga_mock PRIVATE m)
endif()
g2026_configure_host_test(test_fpga_mock)

add_test(NAME g2026_fpga_mock COMMAND test_fpga_mock)

add_executable(test_spectral_logic
    test_spectral_logic.c
    ../User/DSP/g2026_spectral_logic.c
)
target_include_directories(test_spectral_logic PRIVATE ../User/DSP)
g2026_configure_host_test(test_spectral_logic)
if(NOT MSVC)
  target_link_libraries(test_spectral_logic PRIVATE m)
endif()
add_test(NAME g2026_spectral_logic COMMAND test_spectral_logic)

add_executable(test_fpga_protocol
    test_fpga_protocol.c
    ../User/Protocol/fpga_protocol.c
)
target_include_directories(test_fpga_protocol PRIVATE ../User/Protocol)
g2026_configure_host_test(test_fpga_protocol)
add_test(NAME g2026_fpga_protocol COMMAND test_fpga_protocol)

add_executable(test_g2026_fpga_messages
    test_g2026_fpga_messages.c
    ../User/Protocol/g2026_fpga_messages.c
)
target_include_directories(test_g2026_fpga_messages PRIVATE
    ../User/Protocol
)
g2026_configure_host_test(test_g2026_fpga_messages)
add_test(NAME g2026_fpga_messages COMMAND test_g2026_fpga_messages)

add_executable(test_fpga_window_assembler
    test_fpga_window_assembler.c
    ../User/Protocol/g2026_fpga_messages.c
    ../User/Service/fpga_window_assembler.c
)
target_include_directories(test_fpga_window_assembler PRIVATE
    ../User/Protocol
    ../User/Service
    ../User/Config
)
g2026_configure_host_test(test_fpga_window_assembler)
add_test(NAME g2026_fpga_window_assembler
         COMMAND test_fpga_window_assembler)

add_executable(test_g2026_calibration
    test_g2026_calibration.c
    ../User/Config/g2026_calibration.c
)
target_include_directories(test_g2026_calibration PRIVATE ../User/Config)
if(NOT MSVC)
  target_link_libraries(test_g2026_calibration PRIVATE m)
endif()
g2026_configure_host_test(test_g2026_calibration)
add_test(NAME g2026_calibration COMMAND test_g2026_calibration)

add_executable(test_g2026_ad8421_calibration
    test_g2026_ad8421_calibration.c
    ../User/Config/g2026_ad8421_calibration.c
)
target_include_directories(test_g2026_ad8421_calibration PRIVATE
    ../User/Config
)
if(NOT MSVC)
  target_link_libraries(test_g2026_ad8421_calibration PRIVATE m)
endif()
g2026_configure_host_test(test_g2026_ad8421_calibration)
add_test(NAME g2026_ad8421_calibration
         COMMAND test_g2026_ad8421_calibration)

add_executable(test_g2026_sampling_quality
    test_g2026_sampling_quality.c
    ../User/DSP/g2026_sampling_quality.c
)
target_include_directories(test_g2026_sampling_quality PRIVATE ../User/DSP)
g2026_configure_host_test(test_g2026_sampling_quality)
add_test(NAME g2026_sampling_quality COMMAND test_g2026_sampling_quality)

set(G2026_CMSIS_DSP_INCLUDE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../Middlewares/ST/ARM/DSP/Inc"
    CACHE PATH "Directory containing the compatible ST CMSIS-DSP arm_math.h")
if(EXISTS "${G2026_CMSIS_DSP_INCLUDE_DIR}/arm_math.h")
  add_executable(test_g2026_dual_gain
      test_g2026_dual_gain.c
      ../User/DSP/g2026_dual_gain.c
  )
  target_include_directories(test_g2026_dual_gain PRIVATE
      ../User/DSP
      ../User/Config
      "${G2026_CMSIS_DSP_INCLUDE_DIR}"
  )
  if(NOT MSVC)
    target_link_libraries(test_g2026_dual_gain PRIVATE m)
  endif()
  g2026_configure_host_test(test_g2026_dual_gain)
  add_test(NAME g2026_dual_gain COMMAND test_g2026_dual_gain)
else()
  message(STATUS "CMSIS-DSP header absent; historical dual-gain test disabled")
endif()

add_executable(test_g2026_rms_gain_service
    test_g2026_rms_gain_service.c
    ../User/Service/g2026_rms_gain_service.c
)
target_include_directories(test_g2026_rms_gain_service PRIVATE
    ../User/Adapter
    ../User/Service
)
if(NOT MSVC)
  target_link_libraries(test_g2026_rms_gain_service PRIVATE m)
endif()
g2026_configure_host_test(test_g2026_rms_gain_service)
add_test(NAME g2026_rms_gain_service COMMAND test_g2026_rms_gain_service)
