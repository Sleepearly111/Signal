#ifndef CONTEST_APP_H
#define CONTEST_APP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CONTEST_APP_STATUS_OK = 0,
    CONTEST_APP_STATUS_IN_PROGRESS,
    CONTEST_APP_STATUS_INVALID_ARGUMENT,
    CONTEST_APP_STATUS_NOT_INITIALIZED,
    CONTEST_APP_STATUS_BUSY,
    CONTEST_APP_STATUS_NOT_READY,
    CONTEST_APP_STATUS_IO_ERROR
} contest_app_status_t;

typedef enum {
    CONTEST_APP_MODE_BASIC = 0,
    CONTEST_APP_MODE_ADVANCED
} contest_app_mode_t;

typedef enum {
    CONTEST_APP_STATE_UNINITIALIZED = 0,
    CONTEST_APP_STATE_IDLE,
    CONTEST_APP_STATE_RUNNING_BASIC,
    CONTEST_APP_STATE_RUNNING_ADVANCED,
    CONTEST_APP_STATE_RUNNING_FALLBACK,
    CONTEST_APP_STATE_REPORTING,
    CONTEST_APP_STATE_DONE,
    CONTEST_APP_STATE_FAULT,
    CONTEST_APP_STATE_ABORTED
} contest_app_state_t;

typedef enum {
    CONTEST_APP_REASON_NONE = 0,
    CONTEST_APP_REASON_SUCCESS,
    CONTEST_APP_REASON_FALLBACK_SUCCESS,
    CONTEST_APP_REASON_MODE_FAILED,
    CONTEST_APP_REASON_TIMEOUT,
    CONTEST_APP_REASON_REPORT_FAILED,
    CONTEST_APP_REASON_IO_ERROR,
    CONTEST_APP_REASON_ABORTED
} contest_app_reason_t;

typedef enum {
    CONTEST_APP_MODE_OUTCOME_SUCCESS = 0,
    CONTEST_APP_MODE_OUTCOME_FAILED
} contest_app_mode_outcome_t;

typedef enum {
    CONTEST_APP_PORT_OK = 0,
    CONTEST_APP_PORT_ERROR
} contest_app_port_status_t;

typedef struct {
    contest_app_mode_t requested_mode;
} contest_app_request_t;

typedef struct {
    contest_app_mode_t requested_mode;
    contest_app_mode_t completed_mode;
    contest_app_reason_t reason;
    uint32_t mode_result_code;
    bool used_fallback;
} contest_app_result_t;

typedef contest_app_port_status_t (*contest_app_start_mode_fn)(
    void *user_context,
    contest_app_mode_t mode
);

typedef contest_app_port_status_t (*contest_app_service_mode_fn)(
    void *user_context,
    contest_app_mode_t mode,
    uint32_t now_ms
);

typedef contest_app_port_status_t (*contest_app_is_mode_done_fn)(
    void *user_context,
    contest_app_mode_t mode,
    bool *done
);

typedef contest_app_port_status_t (*contest_app_get_mode_result_fn)(
    void *user_context,
    contest_app_mode_t mode,
    contest_app_mode_outcome_t *outcome,
    uint32_t *mode_result_code
);

typedef contest_app_port_status_t (*contest_app_start_report_fn)(
    void *user_context,
    const contest_app_result_t *result
);

typedef contest_app_port_status_t (*contest_app_poll_report_fn)(
    void *user_context,
    bool *done
);

typedef void (*contest_app_safe_stop_fn)(void *user_context);

typedef struct {
    contest_app_start_mode_fn start_mode;
    contest_app_service_mode_fn service_mode;
    contest_app_is_mode_done_fn is_mode_done;
    contest_app_get_mode_result_fn get_mode_result;
    contest_app_start_report_fn start_report;
    contest_app_poll_report_fn poll_report;
    contest_app_safe_stop_fn safe_stop;
    void *user_context;
} contest_app_ops_t;

typedef struct {
    uint32_t total_timeout_ms;
    bool fallback_on_advanced_failure;
} contest_app_config_t;

typedef struct {
    contest_app_ops_t ops;
    contest_app_config_t config;
    contest_app_result_t result;
    contest_app_state_t state;
    uint32_t started_ms;
    bool terminal_success;
    bool initialized;
} contest_app_t;

contest_app_status_t contest_app_init(
    contest_app_t *app,
    const contest_app_config_t *config,
    const contest_app_ops_t *ops
);

contest_app_status_t contest_app_start(
    contest_app_t *app,
    const contest_app_request_t *request,
    uint32_t now_ms
);

contest_app_status_t contest_app_service(
    contest_app_t *app,
    uint32_t now_ms
);

bool contest_app_is_done(const contest_app_t *app);

contest_app_status_t contest_app_get_result(
    const contest_app_t *app,
    contest_app_result_t *result
);

contest_app_status_t contest_app_abort(contest_app_t *app);

contest_app_state_t contest_app_get_state(const contest_app_t *app);

void contest_app_deinit(contest_app_t *app);

#ifdef __cplusplus
}
#endif

#endif
