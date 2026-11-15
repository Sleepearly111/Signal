#ifndef FILTER_IDENTIFICATION_APP_H
#define FILTER_IDENTIFICATION_APP_H

#include "filter_classifier.h"
#include "sweep_service.h"
#include "transfer_response.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FILTER_ID_APP_MAX_POINTS 32U
#define FILTER_ID_APP_MAX_CROSSINGS 8U

typedef enum {
    FILTER_ID_APP_STATUS_OK = 0,
    FILTER_ID_APP_STATUS_IN_PROGRESS,
    FILTER_ID_APP_STATUS_INVALID_ARGUMENT,
    FILTER_ID_APP_STATUS_NOT_INITIALIZED,
    FILTER_ID_APP_STATUS_BUSY,
    FILTER_ID_APP_STATUS_NOT_READY,
    FILTER_ID_APP_STATUS_IO_ERROR
} filter_id_app_status_t;

typedef enum {
    FILTER_ID_APP_STATE_UNINITIALIZED = 0,
    FILTER_ID_APP_STATE_IDLE,
    FILTER_ID_APP_STATE_SWEEPING,
    FILTER_ID_APP_STATE_ANALYZING,
    FILTER_ID_APP_STATE_DONE,
    FILTER_ID_APP_STATE_FAULT,
    FILTER_ID_APP_STATE_ABORTED
} filter_id_app_state_t;

typedef enum {
    FILTER_ID_APP_REASON_NONE = 0,
    FILTER_ID_APP_REASON_SUCCESS,
    FILTER_ID_APP_REASON_AMBIGUOUS,
    FILTER_ID_APP_REASON_SWEEP_FAILED,
    FILTER_ID_APP_REASON_TRANSFER_FAILED,
    FILTER_ID_APP_REASON_CLASSIFIER_FAILED,
    FILTER_ID_APP_REASON_IO_ERROR,
    FILTER_ID_APP_REASON_ABORTED
} filter_id_app_reason_t;

typedef struct {
    sweep_config_t sweep;
    transfer_response_config_t response;
    filter_classifier_config_t classifier;
    float crossing_level_below_peak_db;
} filter_id_app_config_t;

typedef struct {
    const float *frequencies_hz;
    size_t point_count;
} filter_id_app_request_t;

typedef struct {
    filter_type_t type;
    filter_id_app_reason_t reason;
    size_t requested_points;
    size_t measured_points;
    size_t valid_points;
    size_t rejected_points;
    float peak_frequency_hz;
    float notch_frequency_hz;
    float confidence_margin_db;
    float dynamic_span_db;
    size_t crossing_count;
    float crossing_frequencies_hz[FILTER_ID_APP_MAX_CROSSINGS];
    sweep_reason_t sweep_reason;
    transfer_response_status_t transfer_status;
    filter_classifier_status_t classifier_status;
} filter_id_app_result_t;

typedef struct {
    filter_id_app_config_t config;
    sweep_ops_t sweep_ops;
    sweep_service_t sweep;
    filter_id_app_result_t result;
    filter_id_app_state_t state;
    float frequencies_hz[FILTER_ID_APP_MAX_POINTS];
    sweep_point_t sweep_points[FILTER_ID_APP_MAX_POINTS];
    transfer_response_measurement_t
        measurements[FILTER_ID_APP_MAX_POINTS];
    transfer_response_point_t response_points[FILTER_ID_APP_MAX_POINTS];
    float classifier_frequencies_hz[FILTER_ID_APP_MAX_POINTS];
    float classifier_gain_db[FILTER_ID_APP_MAX_POINTS];
    size_t point_count;
    bool initialized;
} filter_id_app_t;

filter_id_app_status_t filter_id_app_init(
    filter_id_app_t *app,
    const filter_id_app_config_t *config,
    const sweep_ops_t *sweep_ops
);
filter_id_app_status_t filter_id_app_start(
    filter_id_app_t *app,
    const filter_id_app_request_t *request,
    uint32_t now_ms
);
filter_id_app_status_t filter_id_app_service(
    filter_id_app_t *app,
    uint32_t now_ms
);
bool filter_id_app_is_done(const filter_id_app_t *app);
filter_id_app_status_t filter_id_app_get_result(
    const filter_id_app_t *app,
    filter_id_app_result_t *result
);
filter_id_app_status_t filter_id_app_abort(filter_id_app_t *app);
filter_id_app_state_t filter_id_app_get_state(
    const filter_id_app_t *app
);
void filter_id_app_deinit(filter_id_app_t *app);

#ifdef __cplusplus
}
#endif

#endif
