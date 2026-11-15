#ifndef REGULATED_SIGNAL_OUTPUT_APP_H
#define REGULATED_SIGNAL_OUTPUT_APP_H

#include "signal_workflow.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    REGULATED_OUTPUT_WAVEFORM_SINE = 0,
    REGULATED_OUTPUT_WAVEFORM_SQUARE,
    REGULATED_OUTPUT_WAVEFORM_TRIANGLE
} regulated_output_waveform_t;

typedef enum {
    REGULATED_OUTPUT_STATUS_OK = 0,
    REGULATED_OUTPUT_STATUS_IN_PROGRESS,
    REGULATED_OUTPUT_STATUS_INVALID_ARGUMENT,
    REGULATED_OUTPUT_STATUS_NOT_INITIALIZED,
    REGULATED_OUTPUT_STATUS_BUSY,
    REGULATED_OUTPUT_STATUS_NOT_READY,
    REGULATED_OUTPUT_STATUS_IO_ERROR
} regulated_output_status_t;

typedef enum {
    REGULATED_OUTPUT_STATE_UNINITIALIZED = 0,
    REGULATED_OUTPUT_STATE_IDLE,
    REGULATED_OUTPUT_STATE_RUNNING,
    REGULATED_OUTPUT_STATE_DONE,
    REGULATED_OUTPUT_STATE_FAULT,
    REGULATED_OUTPUT_STATE_ABORTED
} regulated_output_state_t;

typedef enum {
    REGULATED_OUTPUT_REASON_NONE = 0,
    REGULATED_OUTPUT_REASON_CONVERGED,
    REGULATED_OUTPUT_REASON_TIMEOUT,
    REGULATED_OUTPUT_REASON_MAX_ITERATIONS,
    REGULATED_OUTPUT_REASON_INVALID_MEASUREMENT,
    REGULATED_OUTPUT_REASON_SATURATED,
    REGULATED_OUTPUT_REASON_IO_ERROR,
    REGULATED_OUTPUT_REASON_ABORTED
} regulated_output_reason_t;

typedef enum {
    REGULATED_OUTPUT_PORT_OK = 0,
    REGULATED_OUTPUT_PORT_ERROR
} regulated_output_port_status_t;

typedef regulated_output_port_status_t
(*regulated_output_configure_signal_fn)(
    void *user_context,
    float frequency_hz,
    regulated_output_waveform_t waveform
);

typedef regulated_output_port_status_t
(*regulated_output_set_control_fn)(
    void *user_context,
    float control_value
);

typedef regulated_output_port_status_t
(*regulated_output_start_measurement_fn)(void *user_context);

typedef regulated_output_port_status_t
(*regulated_output_poll_measurement_fn)(
    void *user_context,
    bool *done,
    float *measured_vpp,
    bool *valid,
    bool *saturated
);

typedef void (*regulated_output_safe_stop_fn)(void *user_context);

typedef struct {
    regulated_output_configure_signal_fn configure_signal;
    regulated_output_set_control_fn set_control;
    regulated_output_start_measurement_fn start_measurement;
    regulated_output_poll_measurement_fn poll_measurement;
    regulated_output_safe_stop_fn safe_stop;
    void *user_context;
} regulated_output_ops_t;

typedef struct {
    uint32_t settle_ms;
    uint32_t total_timeout_ms;
    uint32_t max_iterations;
    float minimum_control;
    float maximum_control;
    float minimum_valid_vpp;
    float absolute_tolerance_vpp;
    float relative_tolerance;
    float minimum_correction_ratio;
    float maximum_correction_ratio;
} regulated_output_config_t;

typedef struct {
    float frequency_hz;
    float target_vpp;
    float initial_control;
    regulated_output_waveform_t waveform;
} regulated_output_request_t;

typedef struct {
    float final_vpp;
    float final_control;
    uint32_t iterations;
    regulated_output_reason_t reason;
} regulated_output_result_t;

typedef struct {
    regulated_output_config_t config;
    regulated_output_ops_t ops;
    regulated_output_request_t request;
    regulated_output_result_t result;
    signal_workflow_t workflow;
    float current_control;
    regulated_output_state_t state;
    bool initialized;
} regulated_output_app_t;

regulated_output_status_t regulated_output_app_init(
    regulated_output_app_t *app,
    const regulated_output_config_t *config,
    const regulated_output_ops_t *ops
);

regulated_output_status_t regulated_output_app_start(
    regulated_output_app_t *app,
    const regulated_output_request_t *request,
    uint32_t now_ms
);

regulated_output_status_t regulated_output_app_service(
    regulated_output_app_t *app,
    uint32_t now_ms
);

bool regulated_output_app_is_done(const regulated_output_app_t *app);

regulated_output_status_t regulated_output_app_get_result(
    const regulated_output_app_t *app,
    regulated_output_result_t *result
);

regulated_output_status_t regulated_output_app_abort(
    regulated_output_app_t *app
);

regulated_output_state_t regulated_output_app_get_state(
    const regulated_output_app_t *app
);

void regulated_output_app_deinit(regulated_output_app_t *app);

#ifdef __cplusplus
}
#endif

#endif
