#ifndef SWEEP_SERVICE_H
#define SWEEP_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SWEEP_STATUS_OK = 0,
    SWEEP_STATUS_IN_PROGRESS,
    SWEEP_STATUS_INVALID_ARGUMENT,
    SWEEP_STATUS_NOT_INITIALIZED,
    SWEEP_STATUS_BUSY,
    SWEEP_STATUS_NOT_READY,
    SWEEP_STATUS_IO_ERROR
} sweep_status_t;

typedef enum {
    SWEEP_STATE_UNINITIALIZED = 0,
    SWEEP_STATE_IDLE,
    SWEEP_STATE_SETTLING,
    SWEEP_STATE_MEASURING,
    SWEEP_STATE_DONE,
    SWEEP_STATE_FAULT,
    SWEEP_STATE_ABORTED
} sweep_state_t;

typedef enum {
    SWEEP_REASON_NONE = 0,
    SWEEP_REASON_COMPLETED,
    SWEEP_REASON_POINT_TIMEOUT,
    SWEEP_REASON_TOTAL_TIMEOUT,
    SWEEP_REASON_INVALID_MEASUREMENT,
    SWEEP_REASON_IO_ERROR,
    SWEEP_REASON_ABORTED
} sweep_reason_t;

typedef struct {
    float frequency_hz;
    float input_value;
    float output_value;
    bool valid;
    bool saturated;
} sweep_point_t;

typedef enum {
    SWEEP_PORT_OK = 0,
    SWEEP_PORT_ERROR
} sweep_port_status_t;

typedef sweep_port_status_t (*sweep_set_frequency_fn)(
    void *user_context,
    float frequency_hz
);
typedef sweep_port_status_t (*sweep_start_measurement_fn)(void *user_context);
typedef sweep_port_status_t (*sweep_poll_measurement_fn)(
    void *user_context,
    bool *done,
    sweep_point_t *point
);
typedef void (*sweep_safe_stop_fn)(void *user_context);

typedef struct {
    sweep_set_frequency_fn set_frequency;
    sweep_start_measurement_fn start_measurement;
    sweep_poll_measurement_fn poll_measurement;
    sweep_safe_stop_fn safe_stop;
    void *user_context;
} sweep_ops_t;

typedef struct {
    uint32_t settle_ms;
    uint32_t point_timeout_ms;
    uint32_t total_timeout_ms;
    uint32_t max_retries_per_point;
} sweep_config_t;

typedef struct {
    const float *frequencies_hz;
    size_t point_count;
    sweep_point_t *output_points;
    size_t output_capacity;
} sweep_request_t;

typedef struct {
    size_t completed_points;
    size_t failed_index;
    uint32_t retries_used;
    sweep_reason_t reason;
} sweep_result_t;

typedef struct {
    sweep_ops_t ops;
    sweep_config_t config;
    sweep_request_t request;
    sweep_result_t result;
    sweep_state_t state;
    uint32_t started_ms;
    uint32_t state_started_ms;
    uint32_t retry_count;
    bool initialized;
} sweep_service_t;

sweep_status_t sweep_service_init(
    sweep_service_t *service,
    const sweep_config_t *config,
    const sweep_ops_t *ops
);
sweep_status_t sweep_service_start(
    sweep_service_t *service,
    const sweep_request_t *request,
    uint32_t now_ms
);
sweep_status_t sweep_service_service(
    sweep_service_t *service,
    uint32_t now_ms
);
bool sweep_service_is_done(const sweep_service_t *service);
sweep_status_t sweep_service_get_result(
    const sweep_service_t *service,
    sweep_result_t *result
);
sweep_status_t sweep_service_abort(sweep_service_t *service);
sweep_state_t sweep_service_get_state(const sweep_service_t *service);
void sweep_service_deinit(sweep_service_t *service);

#ifdef __cplusplus
}
#endif

#endif
