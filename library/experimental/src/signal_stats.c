#include "signal_stats.h"

#include <float.h>
#include <stdlib.h>
#include <string.h>

static int signal_stats_is_finite(float value)
{
    return value >= -FLT_MAX && value <= FLT_MAX;
}

static int signal_stats_compare_f32(const void *left, const void *right)
{
    const float a = *(const float *)left;
    const float b = *(const float *)right;
    return (a > b) - (a < b);
}

static size_t signal_stats_nearest_rank_index(size_t count, float percentile)
{
    const double scaled_rank = (double)percentile * (double)count;
    size_t one_based_rank;

    if (percentile <= 0.0f) {
        return 0u;
    }
    if (percentile >= 1.0f) {
        return count - 1u;
    }

    one_based_rank = (size_t)scaled_rank;
    if ((double)one_based_rank < scaled_rank) {
        ++one_based_rank;
    }

    if (one_based_rank > count) {
        one_based_rank = count;
    }
    return one_based_rank - 1u;
}

static signal_stats_status_t signal_stats_validate_percentile(float percentile)
{
    if (!signal_stats_is_finite(percentile) || percentile < 0.0f || percentile > 1.0f) {
        return SIGNAL_STATS_STATUS_PERCENTILE_OUT_OF_RANGE;
    }
    return SIGNAL_STATS_STATUS_OK;
}

signal_stats_status_t signal_stats_percentile_u12(
    const uint16_t *samples,
    size_t count,
    float percentile,
    size_t *histogram,
    size_t histogram_count,
    float *result)
{
    signal_stats_status_t status;
    size_t target_index;
    size_t accumulated = 0u;
    size_t i;

    if (result == NULL) {
        return SIGNAL_STATS_STATUS_INVALID_ARGUMENT;
    }
    if (count == 0u) {
        return SIGNAL_STATS_STATUS_EMPTY_INPUT;
    }
    if (samples == NULL || histogram == NULL) {
        return SIGNAL_STATS_STATUS_INVALID_ARGUMENT;
    }
    status = signal_stats_validate_percentile(percentile);
    if (status != SIGNAL_STATS_STATUS_OK) {
        return status;
    }
    if (histogram_count < SIGNAL_STATS_U12_BIN_COUNT) {
        return SIGNAL_STATS_STATUS_WORKSPACE_TOO_SMALL;
    }

    memset(histogram, 0, SIGNAL_STATS_U12_BIN_COUNT * sizeof(histogram[0]));
    for (i = 0u; i < count; ++i) {
        if (samples[i] >= SIGNAL_STATS_U12_BIN_COUNT) {
            return SIGNAL_STATS_STATUS_SAMPLE_OUT_OF_RANGE;
        }
        ++histogram[samples[i]];
    }

    target_index = signal_stats_nearest_rank_index(count, percentile);
    for (i = 0u; i < SIGNAL_STATS_U12_BIN_COUNT; ++i) {
        accumulated += histogram[i];
        if (accumulated > target_index) {
            *result = (float)i;
            return SIGNAL_STATS_STATUS_OK;
        }
    }

    return SIGNAL_STATS_STATUS_INVALID_ARGUMENT;
}

signal_stats_status_t signal_stats_percentile_f32(
    const float *samples,
    size_t count,
    float percentile,
    float *workspace,
    size_t workspace_count,
    float *result)
{
    signal_stats_status_t status;
    size_t i;

    if (result == NULL) {
        return SIGNAL_STATS_STATUS_INVALID_ARGUMENT;
    }
    if (count == 0u) {
        return SIGNAL_STATS_STATUS_EMPTY_INPUT;
    }
    if (samples == NULL || workspace == NULL) {
        return SIGNAL_STATS_STATUS_INVALID_ARGUMENT;
    }
    status = signal_stats_validate_percentile(percentile);
    if (status != SIGNAL_STATS_STATUS_OK) {
        return status;
    }
    if (workspace_count < count) {
        return SIGNAL_STATS_STATUS_WORKSPACE_TOO_SMALL;
    }
    for (i = 0u; i < count; ++i) {
        if (!signal_stats_is_finite(samples[i])) {
            return SIGNAL_STATS_STATUS_NON_FINITE_INPUT;
        }
    }

    memcpy(workspace, samples, count * sizeof(workspace[0]));
    qsort(workspace, count, sizeof(workspace[0]), signal_stats_compare_f32);
    *result = workspace[signal_stats_nearest_rank_index(count, percentile)];
    return SIGNAL_STATS_STATUS_OK;
}

signal_stats_status_t signal_stats_mid_fraction_u16(
    const uint16_t *samples,
    size_t count,
    float lower_bound,
    float upper_bound,
    float *result)
{
    const float lower_threshold = lower_bound + 0.3f * (upper_bound - lower_bound);
    const float upper_threshold = lower_bound + 0.7f * (upper_bound - lower_bound);
    size_t inside_count = 0u;
    size_t i;

    if (result == NULL) {
        return SIGNAL_STATS_STATUS_INVALID_ARGUMENT;
    }
    if (count == 0u) {
        return SIGNAL_STATS_STATUS_EMPTY_INPUT;
    }
    if (samples == NULL || !signal_stats_is_finite(lower_bound) ||
        !signal_stats_is_finite(upper_bound) || lower_bound >= upper_bound) {
        return SIGNAL_STATS_STATUS_INVALID_ARGUMENT;
    }

    for (i = 0u; i < count; ++i) {
        const float sample = (float)samples[i];
        if (sample > lower_threshold && sample < upper_threshold) {
            ++inside_count;
        }
    }
    *result = (float)inside_count / (float)count;
    return SIGNAL_STATS_STATUS_OK;
}

signal_stats_status_t signal_stats_mid_fraction_f32(
    const float *samples,
    size_t count,
    float lower_bound,
    float upper_bound,
    float *result)
{
    const float lower_threshold = lower_bound + 0.3f * (upper_bound - lower_bound);
    const float upper_threshold = lower_bound + 0.7f * (upper_bound - lower_bound);
    size_t inside_count = 0u;
    size_t i;

    if (result == NULL) {
        return SIGNAL_STATS_STATUS_INVALID_ARGUMENT;
    }
    if (count == 0u) {
        return SIGNAL_STATS_STATUS_EMPTY_INPUT;
    }
    if (samples == NULL || !signal_stats_is_finite(lower_bound) ||
        !signal_stats_is_finite(upper_bound) || lower_bound >= upper_bound) {
        return SIGNAL_STATS_STATUS_INVALID_ARGUMENT;
    }

    for (i = 0u; i < count; ++i) {
        if (!signal_stats_is_finite(samples[i])) {
            return SIGNAL_STATS_STATUS_NON_FINITE_INPUT;
        }
        if (samples[i] > lower_threshold && samples[i] < upper_threshold) {
            ++inside_count;
        }
    }
    *result = (float)inside_count / (float)count;
    return SIGNAL_STATS_STATUS_OK;
}
