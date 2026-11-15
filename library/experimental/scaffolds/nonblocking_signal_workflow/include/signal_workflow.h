#ifndef SIGNAL_WORKFLOW_H
#define SIGNAL_WORKFLOW_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SIGNAL_WORKFLOW_STATUS_OK = 0,
    SIGNAL_WORKFLOW_STATUS_IN_PROGRESS,
    SIGNAL_WORKFLOW_STATUS_INVALID_ARGUMENT,
    SIGNAL_WORKFLOW_STATUS_NOT_INITIALIZED,
    SIGNAL_WORKFLOW_STATUS_BUSY,
    SIGNAL_WORKFLOW_STATUS_NOT_READY,
    SIGNAL_WORKFLOW_STATUS_IO_ERROR
} signal_workflow_status_t;

typedef enum {
    SIGNAL_WORKFLOW_STATE_UNINITIALIZED = 0,
    SIGNAL_WORKFLOW_STATE_IDLE,
    SIGNAL_WORKFLOW_STATE_SETTLING,
    SIGNAL_WORKFLOW_STATE_MEASURING,
    SIGNAL_WORKFLOW_STATE_COMPUTING,
    SIGNAL_WORKFLOW_STATE_DONE,
    SIGNAL_WORKFLOW_STATE_FAULT,
    SIGNAL_WORKFLOW_STATE_ABORTED
} signal_workflow_state_t;

typedef enum {
    SIGNAL_WORKFLOW_REASON_NONE = 0,
    SIGNAL_WORKFLOW_REASON_CONVERGED,
    SIGNAL_WORKFLOW_REASON_TIMEOUT,
    SIGNAL_WORKFLOW_REASON_MAX_ITERATIONS,
    SIGNAL_WORKFLOW_REASON_INVALID_MEASUREMENT,
    SIGNAL_WORKFLOW_REASON_SATURATED,
    SIGNAL_WORKFLOW_REASON_IO_ERROR,
    SIGNAL_WORKFLOW_REASON_ABORTED
} signal_workflow_reason_t;

typedef struct {
    float target;
    uint32_t mode;
} signal_workflow_request_t;

typedef struct {
    float value;
    bool valid;
    bool saturated;
} signal_workflow_measurement_t;

typedef struct {
    float final_value;
    uint32_t iterations;
    signal_workflow_reason_t reason;
} signal_workflow_result_t;

typedef enum {
    SIGNAL_WORKFLOW_PORT_OK = 0,
    SIGNAL_WORKFLOW_PORT_ERROR
} signal_workflow_port_status_t;

typedef signal_workflow_port_status_t (*signal_workflow_apply_request_fn)(
    void *user_context,
    const signal_workflow_request_t *request
);

typedef signal_workflow_port_status_t (*signal_workflow_start_measurement_fn)(
    void *user_context
);

typedef signal_workflow_port_status_t (*signal_workflow_poll_measurement_fn)(
    void *user_context,
    bool *done,
    signal_workflow_measurement_t *measurement
);

typedef signal_workflow_port_status_t (*signal_workflow_compute_fn)(
    void *user_context,
    const signal_workflow_request_t *request,
    const signal_workflow_measurement_t *measurement,
    float *correction,
    bool *converged
);

typedef signal_workflow_port_status_t (*signal_workflow_apply_correction_fn)(
    void *user_context,
    float correction
);

typedef void (*signal_workflow_safe_stop_fn)(void *user_context);

typedef struct {
    signal_workflow_apply_request_fn apply_request;
    signal_workflow_start_measurement_fn start_measurement;
    signal_workflow_poll_measurement_fn poll_measurement;
    signal_workflow_compute_fn compute;
    signal_workflow_apply_correction_fn apply_correction;
    signal_workflow_safe_stop_fn safe_stop;
    void *user_context;
} signal_workflow_ops_t;

typedef struct {
    uint32_t settle_ms;
    uint32_t total_timeout_ms;
    uint32_t max_iterations;
} signal_workflow_config_t;

typedef struct {
    signal_workflow_ops_t ops;
    signal_workflow_config_t config;
    signal_workflow_request_t request;
    signal_workflow_measurement_t measurement;
    signal_workflow_result_t result;
    signal_workflow_state_t state;
    uint32_t started_ms;
    uint32_t state_started_ms;
    bool initialized;
} signal_workflow_t;

signal_workflow_status_t signal_workflow_init(
    signal_workflow_t *workflow,
    const signal_workflow_config_t *config,
    const signal_workflow_ops_t *ops
);

signal_workflow_status_t signal_workflow_start(
    signal_workflow_t *workflow,
    const signal_workflow_request_t *request,
    uint32_t now_ms
);

signal_workflow_status_t signal_workflow_service(
    signal_workflow_t *workflow,
    uint32_t now_ms
);

bool signal_workflow_is_done(const signal_workflow_t *workflow);

signal_workflow_status_t signal_workflow_get_result(
    const signal_workflow_t *workflow,
    signal_workflow_result_t *result
);

signal_workflow_status_t signal_workflow_abort(signal_workflow_t *workflow);

signal_workflow_state_t signal_workflow_get_state(
    const signal_workflow_t *workflow
);

void signal_workflow_deinit(signal_workflow_t *workflow);

#ifdef __cplusplus
}
#endif

#endif
