#include "fpga_mock_suite.h"

#include "g2026_mock_profile.h"

#define COMPONENT(frequency, peak) \
  {                                \
    (frequency), (peak), 0.0F      \
  }

#define COMPONENT_PHASE(frequency, peak, phase) \
  {                                             \
    (frequency), (peak), (phase)                \
  }

#define SIGNAL(actual_rate, gain_ppm_value, dc_mv_value, noise_mv_value) \
  {                                                                      \
    .components = {                                                      \
        COMPONENT(25000.0F, 34.0F),                                      \
        COMPONENT(250000.0F, 15.0F),                                     \
        COMPONENT(500000.0F, 7.0F),                                      \
    },                                                                   \
    .actual_sample_rate_hz = (actual_rate),                              \
    .reported_sample_rate_hz = 2000000U,                                 \
    .noise_seed = G2026_MOCK_NOISE_SEED,                                 \
    .millivolts_per_code = G2026_MOCK_MILLIVOLTS_PER_CODE,               \
    .gain_error_ppm = (gain_ppm_value),                                  \
    .dc_offset_mv = (dc_mv_value),                                       \
    .noise_peak_mv = (noise_mv_value),                                   \
    .component_count = 3U,                                               \
  }

#define NOISE_SIGNAL(noise_mv_value)                       \
  {                                                        \
    .components = {                                        \
        COMPONENT(20000.0F, 28.0F),                        \
        COMPONENT(200000.0F, 6.0F),                        \
        COMPONENT(400000.0F, 5.0F),                        \
    },                                                     \
    .actual_sample_rate_hz = 2000000U,                     \
    .reported_sample_rate_hz = 2000000U,                   \
    .noise_seed = G2026_MOCK_NOISE_SEED,                   \
    .millivolts_per_code = G2026_MOCK_MILLIVOLTS_PER_CODE, \
    .gain_error_ppm = 0.0F,                                \
    .dc_offset_mv = 0.20F,                                 \
    .noise_peak_mv = (noise_mv_value),                     \
    .component_count = 3U,                                 \
  }

static const FpgaMockTestCase test_cases[] = {
    {
        .name = "低次谐波基准",
        .signal = {
            .components = {
                COMPONENT(12500.0F, 38.0F),
                COMPONENT(37500.0F, 16.0F),
                COMPONENT(50000.0F, 9.0F),
            },
            .actual_sample_rate_hz = 2000000U,
            .reported_sample_rate_hz = 2000000U,
            .noise_seed = G2026_MOCK_NOISE_SEED,
            .millivolts_per_code =
                G2026_MOCK_MILLIVOLTS_PER_CODE,
            .gain_error_ppm = 0.0F,
            .dc_offset_mv = 0.0F,
            .noise_peak_mv = 0.0F,
            .component_count = 3U,
        },
    },
    {
        .name = "10次和20次谐波基准",
        .signal = SIGNAL(2000000U, 0.0F, 0.0F, 0.0F),
    },
    {
        .name = "20次谐波 时钟偏差200ppm",
        .signal = SIGNAL(1999600U, 0.0F, 0.10F, 0.12F),
    },
    {
        .name = "20次谐波 时钟偏差400ppm",
        .signal = SIGNAL(1999200U, 0.0F, 0.20F, 0.25F),
    },
    {
        .name = "20次谐波 时钟偏差450ppm",
        .signal = SIGNAL(1999100U, 0.0F, 0.20F, 0.25F),
    },
    {
        .name = "20次谐波 时钟偏差500ppm",
        .signal = SIGNAL(1999000U, 0.0F, 0.25F, 0.35F),
    },
    {
        .name = "20次谐波 时钟偏差550ppm",
        .signal = SIGNAL(1998900U, 0.0F, 0.25F, 0.35F),
    },
    {
        .name = "20次谐波 时钟偏差600ppm",
        .signal = SIGNAL(1998800U, 0.0F, 0.30F, 0.50F),
    },
    {
        .name = "幅值误差-2%",
        .signal = SIGNAL(2000000U, -20000.0F, 0.20F, 0.25F),
    },
    {
        .name = "幅值误差-4%",
        .signal = SIGNAL(2000000U, -40000.0F, 0.35F, 0.50F),
    },
    {
        .name = "幅值误差-4.5%",
        .signal = SIGNAL(2000000U, -45000.0F, 0.35F, 0.50F),
    },
    {
        .name = "幅值误差-5%",
        .signal = SIGNAL(2000000U, -50000.0F, 0.40F, 0.75F),
    },
    {
        .name = "幅值误差-5.5%",
        .signal = SIGNAL(2000000U, -55000.0F, 0.45F, 0.75F),
    },
    {
        .name = "幅值误差-6%",
        .signal = SIGNAL(2000000U, -60000.0F, 0.50F, 1.00F),
    },
    {
        .name = "仅一个谐波",
        .signal = {
            .components = {
                COMPONENT(20000.0F, 30.0F),
                COMPONENT(100000.0F, 12.0F),
            },
            .actual_sample_rate_hz = 2000000U,
            .reported_sample_rate_hz = 2000000U,
            .noise_seed = G2026_MOCK_NOISE_SEED,
            .millivolts_per_code =
                G2026_MOCK_MILLIVOLTS_PER_CODE,
            .gain_error_ppm = 0.0F,
            .dc_offset_mv = 0.10F,
            .noise_peak_mv = 0.12F,
            .component_count = 2U,
        },
    },
    {
        .name = "谐波幅值大于基波",
        .signal = {
            .components = {
                COMPONENT(20000.0F, 8.0F),
                COMPONENT(60000.0F, 32.0F),
                COMPONENT(140000.0F, 18.0F),
            },
            .actual_sample_rate_hz = 2000000U,
            .reported_sample_rate_hz = 2000000U,
            .noise_seed = G2026_MOCK_NOISE_SEED,
            .millivolts_per_code =
                G2026_MOCK_MILLIVOLTS_PER_CODE,
            .gain_error_ppm = 0.0F,
            .dc_offset_mv = 0.10F,
            .noise_peak_mv = 0.12F,
            .component_count = 3U,
        },
    },
    {
        .name = "5mV最小分量",
        .signal = {
            .components = {
                COMPONENT(20000.0F, 30.0F),
                COMPONENT(200000.0F, 5.0F),
                COMPONENT(400000.0F, 5.0F),
            },
            .actual_sample_rate_hz = 2000000U,
            .reported_sample_rate_hz = 2000000U,
            .noise_seed = G2026_MOCK_NOISE_SEED,
            .millivolts_per_code =
                G2026_MOCK_MILLIVOLTS_PER_CODE,
            .gain_error_ppm = 0.0F,
            .dc_offset_mv = 0.15F,
            .noise_peak_mv = 0.15F,
            .component_count = 3U,
        },
    },
    {
        .name = "不同相位峰峰值",
        .signal = {
            .components = {
                COMPONENT_PHASE(25000.0F, 34.0F, 0.0F),
                COMPONENT_PHASE(75000.0F, 14.0F, 90.0F),
                COMPONENT_PHASE(200000.0F, 9.0F, -45.0F),
            },
            .actual_sample_rate_hz = 2000000U,
            .reported_sample_rate_hz = 2000000U,
            .noise_seed = G2026_MOCK_NOISE_SEED,
            .millivolts_per_code =
                G2026_MOCK_MILLIVOLTS_PER_CODE,
            .gain_error_ppm = 0.0F,
            .dc_offset_mv = 0.0F,
            .noise_peak_mv = 0.12F,
            .component_count = 3U,
        },
    },
    {
        .name = "200kHz高基波",
        .signal = {
            .components = {
                COMPONENT(200000.0F, 28.0F),
                COMPONENT(400000.0F, 12.0F),
            },
            .actual_sample_rate_hz = 2000000U,
            .reported_sample_rate_hz = 2000000U,
            .noise_seed = G2026_MOCK_NOISE_SEED,
            .millivolts_per_code =
                G2026_MOCK_MILLIVOLTS_PER_CODE,
            .gain_error_ppm = 0.0F,
            .dc_offset_mv = 0.15F,
            .noise_peak_mv = 0.25F,
            .component_count = 2U,
        },
    },
    {
        .name = "50次500kHz谐波",
        .signal = {
            .components = {
                COMPONENT(10000.0F, 25.0F),
                COMPONENT(250000.0F, 10.0F),
                COMPONENT(500000.0F, 6.0F),
            },
            .actual_sample_rate_hz = 2000000U,
            .reported_sample_rate_hz = 2000000U,
            .noise_seed = G2026_MOCK_NOISE_SEED,
            .millivolts_per_code =
                G2026_MOCK_MILLIVOLTS_PER_CODE,
            .gain_error_ppm = 0.0F,
            .dc_offset_mv = 0.10F,
            .noise_peak_mv = 0.25F,
            .component_count = 3U,
        },
    },
    {
        .name = "5mV分量加1mV噪声",
        .signal = NOISE_SIGNAL(1.0F),
    },
    {
        .name = "5mV分量加20mV噪声",
        .signal = NOISE_SIGNAL(20.0F),
    },
    {
        .name = "5mV分量加25mV噪声",
        .signal = NOISE_SIGNAL(25.0F),
    },
    {
        .name = "5mV分量加30mV噪声",
        .signal = NOISE_SIGNAL(30.0F),
    },
    {
        .name = "5mV分量加强噪声",
        .signal = NOISE_SIGNAL(35.0F),
    },
};

size_t FpgaMockSuite_Count(void)
{
  return sizeof(test_cases) / sizeof(test_cases[0]);
}

const FpgaMockTestCase *FpgaMockSuite_Get(size_t index)
{
  if (index >= FpgaMockSuite_Count())
  {
    return NULL;
  }
  return &test_cases[index];
}
