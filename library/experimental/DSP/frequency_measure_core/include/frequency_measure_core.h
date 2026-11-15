#ifndef FREQUENCY_MEASURE_CORE_H
#define FREQUENCY_MEASURE_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    FM_STATUS_OK = 0,
    FM_STATUS_NULL_ARGUMENT,
    FM_STATUS_INVALID_CONFIG,
    FM_STATUS_EMPTY_INPUT,
    FM_STATUS_INSUFFICIENT_INPUT,
    FM_STATUS_OUTPUT_TOO_SMALL,
    FM_STATUS_VALUE_OUT_OF_RANGE,
    FM_STATUS_ARITHMETIC_OVERFLOW
} fm_status_t;

typedef enum
{
    FM_QUALITY_EMPTY = 0,
    FM_QUALITY_INSUFFICIENT,
    FM_QUALITY_INVALID,
    FM_QUALITY_DEGRADED,
    FM_QUALITY_GOOD
} fm_quality_t;

typedef struct
{
    uint64_t timer_clock_hz;
    uint8_t counter_bits;
    uint32_t cycles_per_capture_interval;
    size_t merge_length;
    uint64_t min_valid_interval_ticks;
    uint64_t max_valid_interval_ticks;
} fm_config_t;

typedef struct
{
    size_t input_interval_count;

    size_t zero_interval_count;
    size_t short_interval_count;
    size_t normal_interval_count;
    size_t long_interval_count;

    uint64_t min_nonzero_interval_ticks;
    uint64_t max_interval_ticks;

    size_t complete_window_count;
    size_t valid_frequency_count;
    size_t invalid_frequency_count;
    size_t trailing_interval_count;

    fm_quality_t quality;
} fm_result_t;

fm_status_t fm_intervals_from_captures_u32(
    const fm_config_t *config,
    const uint32_t *captures,
    size_t capture_count,
    uint64_t *intervals,
    size_t interval_capacity,
    size_t *interval_count);

fm_status_t fm_measure_intervals_u64(
    const fm_config_t *config,
    const uint64_t *intervals,
    size_t interval_count,
    double *frequency_hz,
    size_t frequency_capacity,
    fm_result_t *result);

#ifdef __cplusplus
}
#endif

#endif
