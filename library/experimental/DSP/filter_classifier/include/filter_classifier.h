#ifndef FILTER_CLASSIFIER_H
#define FILTER_CLASSIFIER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FILTER_CLASSIFIER_OK = 0,
    FILTER_CLASSIFIER_INVALID_ARGUMENT,
    FILTER_CLASSIFIER_INVALID_DATA,
    FILTER_CLASSIFIER_AMBIGUOUS
} filter_classifier_status_t;

typedef enum {
    FILTER_TYPE_UNKNOWN = 0,
    FILTER_TYPE_LOW_PASS,
    FILTER_TYPE_HIGH_PASS,
    FILTER_TYPE_BAND_PASS,
    FILTER_TYPE_BAND_STOP
} filter_type_t;

typedef struct {
    float passband_min_db;
    float stopband_max_db;
    float min_contrast_db;
    size_t edge_window_points;
} filter_classifier_config_t;

typedef struct {
    filter_type_t type;
    float confidence_margin_db;
    size_t peak_index;
    size_t notch_index;
    float low_edge_db;
    float high_edge_db;
    float peak_db;
    float notch_db;
} filter_classifier_result_t;

filter_classifier_status_t filter_classifier_classify(
    const float *frequencies_hz,
    const float *gain_db,
    size_t count,
    const filter_classifier_config_t *config,
    filter_classifier_result_t *result
);

#ifdef __cplusplus
}
#endif

#endif
