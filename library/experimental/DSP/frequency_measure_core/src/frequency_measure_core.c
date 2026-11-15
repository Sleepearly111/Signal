#include "frequency_measure_core.h"

#include <limits.h>
#include <string.h>

static int fm_config_is_valid(const fm_config_t *config)
{
    if (config == NULL)
    {
        return 0;
    }

    if ((config->timer_clock_hz == 0U) ||
        (config->counter_bits == 0U) ||
        (config->counter_bits > 32U) ||
        (config->cycles_per_capture_interval == 0U) ||
        (config->merge_length == 0U) ||
        (config->min_valid_interval_ticks == 0U) ||
        (config->min_valid_interval_ticks >
         config->max_valid_interval_ticks))
    {
        return 0;
    }

    return 1;
}

static uint64_t fm_counter_modulus(uint8_t counter_bits)
{
    return UINT64_C(1) << counter_bits;
}

static void fm_result_reset(fm_result_t *result)
{
    memset(result, 0, sizeof(*result));
    result->quality = FM_QUALITY_EMPTY;
}

fm_status_t fm_intervals_from_captures_u32(
    const fm_config_t *config,
    const uint32_t *captures,
    size_t capture_count,
    uint64_t *intervals,
    size_t interval_capacity,
    size_t *interval_count)
{
    uint64_t modulus;
    uint64_t mask;
    size_t required;
    size_t index;

    if (interval_count == NULL)
    {
        return FM_STATUS_NULL_ARGUMENT;
    }
    *interval_count = 0U;

    if ((config == NULL) || (captures == NULL) || (intervals == NULL))
    {
        return FM_STATUS_NULL_ARGUMENT;
    }
    if (!fm_config_is_valid(config))
    {
        return FM_STATUS_INVALID_CONFIG;
    }
    if (capture_count == 0U)
    {
        return FM_STATUS_EMPTY_INPUT;
    }
    if (capture_count < 2U)
    {
        return FM_STATUS_INSUFFICIENT_INPUT;
    }

    required = capture_count - 1U;
    if (interval_capacity < required)
    {
        return FM_STATUS_OUTPUT_TOO_SMALL;
    }

    modulus = fm_counter_modulus(config->counter_bits);
    mask = modulus - UINT64_C(1);

    for (index = 0U; index < capture_count; ++index)
    {
        if ((uint64_t)captures[index] > mask)
        {
            return FM_STATUS_VALUE_OUT_OF_RANGE;
        }
    }

    for (index = 0U; index < required; ++index)
    {
        uint64_t previous = captures[index];
        uint64_t current = captures[index + 1U];

        intervals[index] = (current - previous) & mask;
    }

    *interval_count = required;
    return FM_STATUS_OK;
}

fm_status_t fm_measure_intervals_u64(
    const fm_config_t *config,
    const uint64_t *intervals,
    size_t interval_count,
    double *frequency_hz,
    size_t frequency_capacity,
    fm_result_t *result)
{
    fm_result_t local_result;
    size_t maximum_output_count;
    size_t index;
    size_t window_index;

    if (result == NULL)
    {
        return FM_STATUS_NULL_ARGUMENT;
    }
    fm_result_reset(result);

    if ((config == NULL) || (intervals == NULL) || (frequency_hz == NULL))
    {
        return FM_STATUS_NULL_ARGUMENT;
    }
    if (!fm_config_is_valid(config))
    {
        return FM_STATUS_INVALID_CONFIG;
    }
    if (interval_count == 0U)
    {
        return FM_STATUS_EMPTY_INPUT;
    }

    maximum_output_count = interval_count / config->merge_length;
    if (maximum_output_count == 0U)
    {
        result->input_interval_count = interval_count;
        result->trailing_interval_count = interval_count;
        result->quality = FM_QUALITY_INSUFFICIENT;
        return FM_STATUS_INSUFFICIENT_INPUT;
    }
    if (frequency_capacity < maximum_output_count)
    {
        return FM_STATUS_OUTPUT_TOO_SMALL;
    }

    fm_result_reset(&local_result);
    local_result.input_interval_count = interval_count;
    local_result.complete_window_count = maximum_output_count;
    local_result.trailing_interval_count =
        interval_count % config->merge_length;

    for (index = 0U; index < interval_count; ++index)
    {
        uint64_t interval = intervals[index];

        if (interval > local_result.max_interval_ticks)
        {
            local_result.max_interval_ticks = interval;
        }
        if ((interval != 0U) &&
            ((local_result.min_nonzero_interval_ticks == 0U) ||
             (interval < local_result.min_nonzero_interval_ticks)))
        {
            local_result.min_nonzero_interval_ticks = interval;
        }

        if (interval == 0U)
        {
            local_result.zero_interval_count++;
        }
        else if (interval < config->min_valid_interval_ticks)
        {
            local_result.short_interval_count++;
        }
        else if (interval > config->max_valid_interval_ticks)
        {
            local_result.long_interval_count++;
        }
        else
        {
            local_result.normal_interval_count++;
        }
    }

    for (window_index = 0U;
         window_index < maximum_output_count;
         ++window_index)
    {
        uint64_t tick_sum = 0U;
        int valid = 1;
        size_t offset;

        for (offset = 0U; offset < config->merge_length; ++offset)
        {
            uint64_t interval =
                intervals[window_index * config->merge_length + offset];

            if ((interval == 0U) ||
                (interval < config->min_valid_interval_ticks) ||
                (interval > config->max_valid_interval_ticks))
            {
                valid = 0;
                continue;
            }
            if (UINT64_MAX - tick_sum < interval)
            {
                fm_result_reset(result);
                return FM_STATUS_ARITHMETIC_OVERFLOW;
            }
            tick_sum += interval;
        }

        if (!valid)
        {
            local_result.invalid_frequency_count++;
            continue;
        }

        frequency_hz[local_result.valid_frequency_count] =
            ((double)config->timer_clock_hz *
             (double)config->cycles_per_capture_interval *
             (double)config->merge_length) /
            (double)tick_sum;
        local_result.valid_frequency_count++;
    }

    if (local_result.valid_frequency_count == 0U)
    {
        local_result.quality = FM_QUALITY_INVALID;
    }
    else if (local_result.invalid_frequency_count != 0U)
    {
        local_result.quality = FM_QUALITY_DEGRADED;
    }
    else
    {
        local_result.quality = FM_QUALITY_GOOD;
    }

    *result = local_result;
    return FM_STATUS_OK;
}
