#ifndef TRANSFER_RESPONSE_H
#define TRANSFER_RESPONSE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TRANSFER_RESPONSE_OK = 0,
    TRANSFER_RESPONSE_INVALID_ARGUMENT,
    TRANSFER_RESPONSE_INVALID_DATA,
    TRANSFER_RESPONSE_OUTPUT_TOO_SMALL,
    TRANSFER_RESPONSE_INSUFFICIENT_VALID_POINTS
} transfer_response_status_t;

typedef struct {
    float frequency_hz;
    float input_value;
    float output_value;
    bool valid;
    bool input_saturated;
    bool output_saturated;
} transfer_response_measurement_t;

typedef struct {
    float minimum_input_value;
    float maximum_gain_linear;
    size_t minimum_valid_points;
} transfer_response_config_t;

typedef struct {
    float frequency_hz;
    float gain_linear;
    float gain_db;
} transfer_response_point_t;

typedef struct {
    size_t valid_count;
    size_t rejected_count;
    size_t peak_index;
    size_t notch_index;
    float peak_gain_db;
    float notch_gain_db;
    float dynamic_span_db;
} transfer_response_summary_t;

transfer_response_status_t transfer_response_prepare(
    const transfer_response_measurement_t *measurements,
    size_t measurement_count,
    const transfer_response_config_t *config,
    transfer_response_point_t *output_points,
    size_t output_capacity,
    transfer_response_summary_t *summary
);

transfer_response_status_t transfer_response_find_level_crossings(
    const transfer_response_point_t *points,
    size_t point_count,
    float level_below_peak_db,
    float *crossing_frequencies_hz,
    size_t crossing_capacity,
    size_t *crossing_count
);

#ifdef __cplusplus
}
#endif

#endif
