#ifndef SIGNAL_STATS_H
#define SIGNAL_STATS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SIGNAL_STATS_U12_BIN_COUNT ((size_t)4096u)

typedef enum {
    SIGNAL_STATS_STATUS_OK = 0,
    SIGNAL_STATS_STATUS_INVALID_ARGUMENT,
    SIGNAL_STATS_STATUS_EMPTY_INPUT,
    SIGNAL_STATS_STATUS_PERCENTILE_OUT_OF_RANGE,
    SIGNAL_STATS_STATUS_WORKSPACE_TOO_SMALL,
    SIGNAL_STATS_STATUS_SAMPLE_OUT_OF_RANGE,
    SIGNAL_STATS_STATUS_NON_FINITE_INPUT
} signal_stats_status_t;

signal_stats_status_t signal_stats_percentile_u12(
    const uint16_t *samples,
    size_t count,
    float percentile,
    size_t *histogram,
    size_t histogram_count,
    float *result);

signal_stats_status_t signal_stats_percentile_f32(
    const float *samples,
    size_t count,
    float percentile,
    float *workspace,
    size_t workspace_count,
    float *result);

signal_stats_status_t signal_stats_mid_fraction_u16(
    const uint16_t *samples,
    size_t count,
    float lower_bound,
    float upper_bound,
    float *result);

signal_stats_status_t signal_stats_mid_fraction_f32(
    const float *samples,
    size_t count,
    float lower_bound,
    float upper_bound,
    float *result);

#ifdef __cplusplus
}
#endif

#endif
