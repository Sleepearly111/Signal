cmake_minimum_required(VERSION 3.20)

project(SignalFrameworkLibrary VERSION 0.1.0 LANGUAGES C)

add_library(signal_framework INTERFACE)
add_library(SignalFramework::signal_framework ALIAS signal_framework)

add_library(signal_framework_experimental_bundle INTERFACE)
add_library(
    SignalFramework::experimental_bundle
    ALIAS
    signal_framework_experimental_bundle
)

target_include_directories(
    signal_framework
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/experimental/include>
)

option(
    SIGNAL_FRAMEWORK_VALIDATE_TEMPLATE
    "Compile the generic experimental module template"
    OFF
)

option(
    SIGNAL_FRAMEWORK_VALIDATE_SIGNAL_WORKFLOW_SCAFFOLD
    "Build and test the nonblocking signal workflow scaffold"
    OFF
)

option(
    SIGNAL_FRAMEWORK_VALIDATE_CONTEST_APP_SCAFFOLD
    "Build and test the contest application scaffold"
    OFF
)

option(SIGNAL_FRAMEWORK_VALIDATE_SWEEP_SERVICE "Build and test the L1 nonblocking sweep service" OFF)
option(SIGNAL_FRAMEWORK_VALIDATE_FILTER_CLASSIFIER "Build and test the L1 conservative filter classifier" OFF)
option(SIGNAL_FRAMEWORK_VALIDATE_CALIBRATION_RECORD "Build and test the L1 versioned calibration record" OFF)
option(SIGNAL_FRAMEWORK_VALIDATE_TRANSFER_RESPONSE "Build and test the L1 Vin/Vout transfer response core" OFF)
option(SIGNAL_FRAMEWORK_VALIDATE_FILTER_ID_PIPELINE "Build the sweep-to-classifier integration test" OFF)
option(SIGNAL_FRAMEWORK_VALIDATE_FILTER_ID_APP "Build the filter identification reference app" OFF)
option(SIGNAL_FRAMEWORK_VALIDATE_REGULATED_OUTPUT_APP "Build the regulated signal output reference app" OFF)

option(
    SIGNAL_FRAMEWORK_BUILD_DSP_STATS
    "Build the experimental DSP-STATS-001 module"
    OFF
)

if(SIGNAL_FRAMEWORK_VALIDATE_FILTER_ID_PIPELINE OR
   SIGNAL_FRAMEWORK_VALIDATE_FILTER_ID_APP)
    set(SIGNAL_FRAMEWORK_VALIDATE_SWEEP_SERVICE ON)
    set(SIGNAL_FRAMEWORK_VALIDATE_TRANSFER_RESPONSE ON)
    set(SIGNAL_FRAMEWORK_VALIDATE_FILTER_CLASSIFIER ON)
endif()

if(SIGNAL_FRAMEWORK_VALIDATE_REGULATED_OUTPUT_APP)
    set(SIGNAL_FRAMEWORK_VALIDATE_SIGNAL_WORKFLOW_SCAFFOLD ON)
endif()

option(SIGNAL_FRAMEWORK_BUILD_DSP_INSTFREQ "Build DSP-INSTFREQ-001" OFF)
option(SIGNAL_FRAMEWORK_BUILD_DSP_BEATFLL "Build DSP-BEATFLL-001" OFF)
option(SIGNAL_FRAMEWORK_BUILD_DSP_RMS "Build the RMS calculation module" OFF)
option(SIGNAL_FRAMEWORK_BUILD_DSP_FFT "Build the FFT tools module" OFF)
option(SIGNAL_FRAMEWORK_BUILD_DSP_LUT "Build the LUT interpolation module" OFF)
option(SIGNAL_FRAMEWORK_BUILD_DSP_TF2 "Build the second-order transfer-function module" OFF)
option(SIGNAL_FRAMEWORK_BUILD_AD603_MODEL "Build the AD603 calibration model" OFF)
option(SIGNAL_FRAMEWORK_BUILD_AD9833 "Build the AD9833 protocol driver" OFF)
option(SIGNAL_FRAMEWORK_BUILD_ADS8688 "Build the ADS8688 protocol driver" OFF)
option(SIGNAL_FRAMEWORK_BUILD_FPGA_PACKET "Build the FPGA packet protocol core" OFF)
option(
    SIGNAL_FRAMEWORK_BUILD_ALL_L2
    "Build every module currently recorded as L2 Software Tested"
    OFF
)

option(
    SIGNAL_FRAMEWORK_BUILD_TESTS
    "Build host tests for enabled experimental modules"
    ON
)

if(SIGNAL_FRAMEWORK_BUILD_ALL_L2)
    set(SIGNAL_FRAMEWORK_BUILD_DSP_STATS ON)
    set(SIGNAL_FRAMEWORK_BUILD_DSP_INSTFREQ ON)
    set(SIGNAL_FRAMEWORK_BUILD_DSP_BEATFLL ON)
    set(SIGNAL_FRAMEWORK_BUILD_DSP_RMS ON)
    set(SIGNAL_FRAMEWORK_BUILD_DSP_FFT ON)
    set(SIGNAL_FRAMEWORK_BUILD_DSP_LUT ON)
    set(SIGNAL_FRAMEWORK_BUILD_DSP_TF2 ON)
    set(SIGNAL_FRAMEWORK_BUILD_AD603_MODEL ON)
    set(SIGNAL_FRAMEWORK_BUILD_AD9833 ON)
    set(SIGNAL_FRAMEWORK_BUILD_ADS8688 ON)
    set(SIGNAL_FRAMEWORK_BUILD_FPGA_PACKET ON)
endif()

function(signal_framework_enable_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

if(SIGNAL_FRAMEWORK_VALIDATE_TEMPLATE)
    add_library(
        signal_framework_template_check
        STATIC
        experimental/template/module.c
    )

    target_include_directories(
        signal_framework_template_check
        PUBLIC
            experimental/template
    )

    target_compile_features(signal_framework_template_check PUBLIC c_std_11)

    signal_framework_enable_warnings(signal_framework_template_check)
endif()

if(SIGNAL_FRAMEWORK_VALIDATE_SIGNAL_WORKFLOW_SCAFFOLD)
    add_subdirectory(
        experimental/scaffolds/nonblocking_signal_workflow
        experimental_build/nonblocking_signal_workflow
    )
endif()

if(SIGNAL_FRAMEWORK_VALIDATE_CONTEST_APP_SCAFFOLD)
    add_subdirectory(
        experimental/scaffolds/contest_app
        experimental_build/contest_app
    )
endif()

if(SIGNAL_FRAMEWORK_VALIDATE_SWEEP_SERVICE)
    add_subdirectory(experimental/services/sweep_service experimental_build/sweep_service)
endif()

if(SIGNAL_FRAMEWORK_VALIDATE_FILTER_CLASSIFIER)
    add_subdirectory(experimental/DSP/filter_classifier experimental_build/filter_classifier)
endif()

if(SIGNAL_FRAMEWORK_VALIDATE_CALIBRATION_RECORD)
    add_subdirectory(experimental/data/calibration_record experimental_build/calibration_record)
endif()

if(SIGNAL_FRAMEWORK_VALIDATE_TRANSFER_RESPONSE)
    add_subdirectory(experimental/DSP/transfer_response experimental_build/transfer_response)
endif()

if(SIGNAL_FRAMEWORK_VALIDATE_FILTER_ID_APP)
    add_subdirectory(
        experimental/reference_apps/filter_identification_app
        experimental_build/filter_identification_app
    )
endif()

if(SIGNAL_FRAMEWORK_VALIDATE_REGULATED_OUTPUT_APP)
    add_subdirectory(
        experimental/reference_apps/regulated_signal_output_app
        experimental_build/regulated_signal_output_app
    )
endif()

if(SIGNAL_FRAMEWORK_VALIDATE_FILTER_ID_PIPELINE)
    add_subdirectory(
        experimental/integration/filter_identification_pipeline
        experimental_build/filter_identification_pipeline
    )
endif()

if(SIGNAL_FRAMEWORK_BUILD_DSP_STATS)
    add_library(
        signal_stats_experimental
        STATIC
        experimental/src/signal_stats.c
    )
    add_library(
        SignalFramework::signal_stats_experimental
        ALIAS
        signal_stats_experimental
    )
    target_include_directories(
        signal_stats_experimental
        PUBLIC
            experimental/include
    )
    target_compile_features(signal_stats_experimental PUBLIC c_std_11)
    signal_framework_enable_warnings(signal_stats_experimental)
    target_link_libraries(
        signal_framework_experimental_bundle
        INTERFACE
            signal_stats_experimental
    )

    if(SIGNAL_FRAMEWORK_BUILD_TESTS)
        enable_testing()
        add_executable(
            signal_stats_tests
            experimental/tests/test_signal_stats.c
        )
        target_link_libraries(signal_stats_tests PRIVATE signal_stats_experimental)
        target_compile_features(signal_stats_tests PRIVATE c_std_11)
        signal_framework_enable_warnings(signal_stats_tests)
        add_test(NAME DSP-STATS-001 COMMAND signal_stats_tests)
    endif()
endif()

if(SIGNAL_FRAMEWORK_BUILD_DSP_INSTFREQ)
    add_subdirectory(
        experimental/DSP/frequency_measure_core
        experimental_build/frequency_measure_core
    )
    target_link_libraries(
        signal_framework_experimental_bundle
        INTERFACE
            frequency_measure_core
    )
endif()

if(SIGNAL_FRAMEWORK_BUILD_DSP_BEATFLL)
    add_subdirectory(
        experimental/DSP/beat_fll_controller
        experimental_build/beat_fll_controller
    )
    target_link_libraries(
        signal_framework_experimental_bundle
        INTERFACE
            beat_fll_controller
    )
endif()

if(SIGNAL_FRAMEWORK_BUILD_DSP_RMS)
    add_subdirectory("experimental/DSP/RMS计算" experimental_build/rms_calc)
    target_link_libraries(
        signal_framework_experimental_bundle
        INTERFACE
            rms_calc
    )
endif()

if(SIGNAL_FRAMEWORK_BUILD_DSP_FFT)
    add_subdirectory("experimental/DSP/FFT工具" experimental_build/fft_tools)
    target_link_libraries(
        signal_framework_experimental_bundle
        INTERFACE
            fft_tools
    )
endif()

if(SIGNAL_FRAMEWORK_BUILD_DSP_LUT)
    add_subdirectory("experimental/DSP/一维查表插值" experimental_build/lut_interp)
    target_link_libraries(
        signal_framework_experimental_bundle
        INTERFACE
            lut_interp
    )
endif()

if(SIGNAL_FRAMEWORK_BUILD_DSP_TF2)
    add_subdirectory("experimental/DSP/二阶传递函数" experimental_build/tf2)
    target_link_libraries(
        signal_framework_experimental_bundle
        INTERFACE
            tf2
    )
endif()

if(SIGNAL_FRAMEWORK_BUILD_AD603_MODEL)
    add_subdirectory(
        "experimental/模拟标定/AD603增益模型"
        experimental_build/ad603_model
    )
    target_link_libraries(
        signal_framework_experimental_bundle
        INTERFACE
            ad603_model
    )
endif()

if(SIGNAL_FRAMEWORK_BUILD_AD9833)
    add_subdirectory("experimental/驱动/AD9833" experimental_build/ad9833)
    target_link_libraries(
        signal_framework_experimental_bundle
        INTERFACE
            ad9833
    )
endif()

if(SIGNAL_FRAMEWORK_BUILD_ADS8688)
    add_subdirectory("experimental/驱动/ADS8688" experimental_build/ads8688)
    target_link_libraries(
        signal_framework_experimental_bundle
        INTERFACE
            ads8688
    )
endif()

if(SIGNAL_FRAMEWORK_BUILD_FPGA_PACKET)
    add_subdirectory("experimental/drivers/fpga_packet" experimental_build/fpga_packet)
    target_link_libraries(
        signal_framework_experimental_bundle
        INTERFACE
            fpga_packet_protocol
    )
endif()
