#include "frequency_measure_core.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

static fm_config_t default_config(void)
{
    fm_config_t config;

    config.timer_clock_hz = UINT64_C(168000000);
    config.counter_bits = 16U;
    config.cycles_per_capture_interval = 8U;
    config.merge_length = 5U;
    config.min_valid_interval_ticks = UINT64_C(200);
    config.max_valid_interval_ticks = UINT64_C(5000);
    return config;
}

static int nearly_equal(double actual, double expected)
{
    double difference = fabs(actual - expected);
    double scale = fabs(expected);
    double tolerance = 1.0e-9 * scale + 1.0e-6;

    return difference <= tolerance;
}

static void test_normal_period(void)
{
    fm_config_t config = default_config();
    const uint64_t intervals[10] = {
        672U, 672U, 672U, 672U, 672U,
        672U, 672U, 672U, 672U, 672U
    };
    double frequency[2] = {-1.0, -1.0};
    fm_result_t result;

    assert(fm_measure_intervals_u64(
               &config, intervals, 10U, frequency, 2U, &result) ==
           FM_STATUS_OK);
    assert(result.valid_frequency_count == 2U);
    assert(result.invalid_frequency_count == 0U);
    assert(result.normal_interval_count == 10U);
    assert(result.zero_interval_count == 0U);
    assert(result.short_interval_count == 0U);
    assert(result.long_interval_count == 0U);
    assert(result.min_nonzero_interval_ticks == 672U);
    assert(result.max_interval_ticks == 672U);
    assert(result.quality == FM_QUALITY_GOOD);
    assert(nearly_equal(frequency[0], 2000000.0));
    assert(nearly_equal(frequency[1], 2000000.0));
}

static void test_counter_wrap_for_multiple_widths(void)
{
    fm_config_t config = default_config();
    uint64_t intervals[2] = {0U, 0U};
    size_t count = 99U;

    {
        const uint32_t captures[3] = {65000U, 136U, 808U};

        assert(fm_intervals_from_captures_u32(
                   &config, captures, 3U, intervals, 2U, &count) ==
               FM_STATUS_OK);
        assert(count == 2U);
        assert(intervals[0] == 672U);
        assert(intervals[1] == 672U);
    }

    {
        const uint32_t captures[2] = {250U, 6U};

        config.counter_bits = 8U;
        assert(fm_intervals_from_captures_u32(
                   &config, captures, 2U, intervals, 2U, &count) ==
               FM_STATUS_OK);
        assert(count == 1U);
        assert(intervals[0] == 12U);
    }

    {
        const uint32_t captures[2] = {0x00FFFF00U, 0x00000100U};

        config.counter_bits = 24U;
        assert(fm_intervals_from_captures_u32(
                   &config, captures, 2U, intervals, 2U, &count) ==
               FM_STATUS_OK);
        assert(intervals[0] == 512U);
    }

    {
        const uint32_t captures[2] = {0xFFFFFF00U, 0x00000100U};

        config.counter_bits = 32U;
        assert(fm_intervals_from_captures_u32(
                   &config, captures, 2U, intervals, 2U, &count) ==
               FM_STATUS_OK);
        assert(intervals[0] == 512U);
    }
}

static void test_zero_short_long_and_degraded_quality(void)
{
    fm_config_t config = default_config();
    const uint64_t intervals[8] = {
        672U, 0U,
        100U, 672U,
        6000U, 672U,
        672U, 672U
    };
    double frequency[4] = {-1.0, -1.0, -1.0, -1.0};
    fm_result_t result;

    config.merge_length = 2U;
    assert(fm_measure_intervals_u64(
               &config, intervals, 8U, frequency, 4U, &result) ==
           FM_STATUS_OK);
    assert(result.complete_window_count == 4U);
    assert(result.valid_frequency_count == 1U);
    assert(result.invalid_frequency_count == 3U);
    assert(result.zero_interval_count == 1U);
    assert(result.short_interval_count == 1U);
    assert(result.long_interval_count == 1U);
    assert(result.normal_interval_count == 5U);
    assert(result.min_nonzero_interval_ticks == 100U);
    assert(result.max_interval_ticks == 6000U);
    assert(result.quality == FM_QUALITY_DEGRADED);
    assert(nearly_equal(frequency[0], 2000000.0));
}

static void test_different_timer_clocks(void)
{
    fm_config_t config = default_config();
    const uint64_t intervals[1] = {672U};
    double frequency[1] = {-1.0};
    fm_result_t result;

    config.merge_length = 1U;
    assert(fm_measure_intervals_u64(
               &config, intervals, 1U, frequency, 1U, &result) ==
           FM_STATUS_OK);
    assert(nearly_equal(frequency[0], 2000000.0));

    config.timer_clock_hz = UINT64_C(84000000);
    assert(fm_measure_intervals_u64(
               &config, intervals, 1U, frequency, 1U, &result) ==
           FM_STATUS_OK);
    assert(nearly_equal(frequency[0], 1000000.0));
}

static void test_different_merge_lengths_and_trailing_data(void)
{
    fm_config_t config = default_config();
    const uint64_t intervals[5] = {670U, 674U, 670U, 674U, 672U};
    double frequency[5] = {-1.0, -1.0, -1.0, -1.0, -1.0};
    fm_result_t result;

    config.merge_length = 1U;
    assert(fm_measure_intervals_u64(
               &config, intervals, 5U, frequency, 5U, &result) ==
           FM_STATUS_OK);
    assert(result.valid_frequency_count == 5U);

    config.merge_length = 2U;
    assert(fm_measure_intervals_u64(
               &config, intervals, 5U, frequency, 2U, &result) ==
           FM_STATUS_OK);
    assert(result.valid_frequency_count == 2U);
    assert(result.trailing_interval_count == 1U);
    assert(nearly_equal(frequency[0], 2000000.0));
    assert(nearly_equal(frequency[1], 2000000.0));

    config.merge_length = 5U;
    assert(fm_measure_intervals_u64(
               &config, intervals, 5U, frequency, 1U, &result) ==
           FM_STATUS_OK);
    assert(result.valid_frequency_count == 1U);
    assert(result.trailing_interval_count == 0U);
    assert(nearly_equal(frequency[0], 2000000.0));
}

static void test_empty_insufficient_and_invalid_quality(void)
{
    fm_config_t config = default_config();
    const uint64_t short_input[2] = {672U, 672U};
    const uint64_t invalid_input[5] = {0U, 100U, 6000U, 0U, 100U};
    double frequency[1] = {-1.0};
    fm_result_t result;

    assert(fm_measure_intervals_u64(
               &config, short_input, 0U, frequency, 1U, &result) ==
           FM_STATUS_EMPTY_INPUT);
    assert(result.quality == FM_QUALITY_EMPTY);

    assert(fm_measure_intervals_u64(
               &config, short_input, 2U, frequency, 1U, &result) ==
           FM_STATUS_INSUFFICIENT_INPUT);
    assert(result.input_interval_count == 2U);
    assert(result.trailing_interval_count == 2U);
    assert(result.quality == FM_QUALITY_INSUFFICIENT);

    assert(fm_measure_intervals_u64(
               &config, invalid_input, 5U, frequency, 1U, &result) ==
           FM_STATUS_OK);
    assert(result.valid_frequency_count == 0U);
    assert(result.invalid_frequency_count == 1U);
    assert(result.quality == FM_QUALITY_INVALID);
}

static void test_invalid_arguments_and_capacity(void)
{
    fm_config_t config = default_config();
    const uint32_t captures[2] = {0U, 672U};
    const uint64_t intervals[5] = {672U, 672U, 672U, 672U, 672U};
    uint64_t converted[1] = {999U};
    double frequency[1] = {999.0};
    size_t count = 999U;
    fm_result_t result;

    assert(fm_intervals_from_captures_u32(
               NULL, captures, 2U, converted, 1U, &count) ==
           FM_STATUS_NULL_ARGUMENT);
    assert(count == 0U);

    config.counter_bits = 0U;
    assert(fm_intervals_from_captures_u32(
               &config, captures, 2U, converted, 1U, &count) ==
           FM_STATUS_INVALID_CONFIG);

    config = default_config();
    assert(fm_intervals_from_captures_u32(
               &config, captures, 2U, converted, 0U, &count) ==
           FM_STATUS_OUTPUT_TOO_SMALL);
    assert(count == 0U);

    {
        const uint32_t out_of_range[2] = {0U, 65536U};

        assert(fm_intervals_from_captures_u32(
                   &config, out_of_range, 2U, converted, 1U, &count) ==
               FM_STATUS_VALUE_OUT_OF_RANGE);
        assert(count == 0U);
    }

    config.timer_clock_hz = 0U;
    assert(fm_measure_intervals_u64(
               &config, intervals, 5U, frequency, 1U, &result) ==
           FM_STATUS_INVALID_CONFIG);
    assert(result.quality == FM_QUALITY_EMPTY);

    config = default_config();
    assert(fm_measure_intervals_u64(
               &config, intervals, 5U, frequency, 0U, &result) ==
           FM_STATUS_OUTPUT_TOO_SMALL);
    assert(result.valid_frequency_count == 0U);
    assert(frequency[0] == 999.0);
}

static void test_arithmetic_overflow(void)
{
    fm_config_t config = default_config();
    const uint64_t intervals[2] = {UINT64_MAX, 1U};
    double frequency[1] = {123.0};
    fm_result_t result;

    config.merge_length = 2U;
    config.min_valid_interval_ticks = 1U;
    config.max_valid_interval_ticks = UINT64_MAX;

    assert(fm_measure_intervals_u64(
               &config, intervals, 2U, frequency, 1U, &result) ==
           FM_STATUS_ARITHMETIC_OVERFLOW);
    assert(result.valid_frequency_count == 0U);
    assert(result.quality == FM_QUALITY_EMPTY);
    assert(frequency[0] == 123.0);
}

int main(void)
{
    test_normal_period();
    test_counter_wrap_for_multiple_widths();
    test_zero_short_long_and_degraded_quality();
    test_different_timer_clocks();
    test_different_merge_lengths_and_trailing_data();
    test_empty_insufficient_and_invalid_quality();
    test_invalid_arguments_and_capacity();
    test_arithmetic_overflow();

    puts("DSP-INSTFREQ-001: all host tests passed");
    return 0;
}
