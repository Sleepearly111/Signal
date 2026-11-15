#include "contest_app.h"

#include <stddef.h>
#include <string.h>

static bool contest_app_elapsed(
    uint32_t now_ms,
    uint32_t start_ms,
    uint32_t duration_ms
)
{
    return (uint32_t)(now_ms - start_ms) >= duration_ms;
}

static bool contest_app_is_terminal(contest_app_state_t state)
{
    return (state == CONTEST_APP_STATE_DONE)
        || (state == CONTEST_APP_STATE_FAULT)
        || (state == CONTEST_APP_STATE_ABORTED);
}

static bool contest_app_is_running(contest_app_state_t state)
{
    return (state == CONTEST_APP_STATE_RUNNING_BASIC)
        || (state == CONTEST_APP_STATE_RUNNING_ADVANCED)
        || (state == CONTEST_APP_STATE_RUNNING_FALLBACK);
}

static contest_app_mode_t contest_app_active_mode(const contest_app_t *app)
{
    return (app->state == CONTEST_APP_STATE_RUNNING_ADVANCED)
        ? CONTEST_APP_MODE_ADVANCED
        : CONTEST_APP_MODE_BASIC;
}

static void contest_app_safe_stop(contest_app_t *app)
{
    app->ops.safe_stop(app->ops.user_context);
}

static contest_app_status_t contest_app_fault(
    contest_app_t *app,
    contest_app_reason_t reason
)
{
    app->result.reason = reason;
    app->state = CONTEST_APP_STATE_FAULT;
    contest_app_safe_stop(app);
    return CONTEST_APP_STATUS_IO_ERROR;
}

static contest_app_status_t contest_app_begin_report(
    contest_app_t *app,
    bool success
)
{
    contest_app_safe_stop(app);
    app->terminal_success = success;
    if (app->ops.start_report(
            app->ops.user_context,
            &app->result) != CONTEST_APP_PORT_OK) {
        app->result.reason = CONTEST_APP_REASON_REPORT_FAILED;
        app->state = CONTEST_APP_STATE_FAULT;
        return CONTEST_APP_STATUS_IO_ERROR;
    }
    app->state = CONTEST_APP_STATE_REPORTING;
    return CONTEST_APP_STATUS_IN_PROGRESS;
}

contest_app_status_t contest_app_init(
    contest_app_t *app,
    const contest_app_config_t *config,
    const contest_app_ops_t *ops
)
{
    if ((app == NULL) || (config == NULL) || (ops == NULL)) {
        return CONTEST_APP_STATUS_INVALID_ARGUMENT;
    }
    if ((config->total_timeout_ms == 0U)
        || (ops->start_mode == NULL)
        || (ops->service_mode == NULL)
        || (ops->is_mode_done == NULL)
        || (ops->get_mode_result == NULL)
        || (ops->start_report == NULL)
        || (ops->poll_report == NULL)
        || (ops->safe_stop == NULL)) {
        return CONTEST_APP_STATUS_INVALID_ARGUMENT;
    }

    (void)memset(app, 0, sizeof(*app));
    app->ops = *ops;
    app->config = *config;
    app->state = CONTEST_APP_STATE_IDLE;
    app->initialized = true;
    return CONTEST_APP_STATUS_OK;
}

contest_app_status_t contest_app_start(
    contest_app_t *app,
    const contest_app_request_t *request,
    uint32_t now_ms
)
{
    contest_app_mode_t mode;

    if ((app == NULL) || (request == NULL)) {
        return CONTEST_APP_STATUS_INVALID_ARGUMENT;
    }
    if (!app->initialized) {
        return CONTEST_APP_STATUS_NOT_INITIALIZED;
    }
    if ((request->requested_mode != CONTEST_APP_MODE_BASIC)
        && (request->requested_mode != CONTEST_APP_MODE_ADVANCED)) {
        return CONTEST_APP_STATUS_INVALID_ARGUMENT;
    }
    if ((app->state != CONTEST_APP_STATE_IDLE)
        && !contest_app_is_terminal(app->state)) {
        return CONTEST_APP_STATUS_BUSY;
    }

    (void)memset(&app->result, 0, sizeof(app->result));
    app->result.requested_mode = request->requested_mode;
    app->result.completed_mode = request->requested_mode;
    app->started_ms = now_ms;
    app->terminal_success = false;
    mode = request->requested_mode;

    if (app->ops.start_mode(
            app->ops.user_context,
            mode) != CONTEST_APP_PORT_OK) {
        return contest_app_fault(app, CONTEST_APP_REASON_IO_ERROR);
    }

    app->state = (mode == CONTEST_APP_MODE_ADVANCED)
        ? CONTEST_APP_STATE_RUNNING_ADVANCED
        : CONTEST_APP_STATE_RUNNING_BASIC;
    return CONTEST_APP_STATUS_OK;
}

contest_app_status_t contest_app_service(
    contest_app_t *app,
    uint32_t now_ms
)
{
    contest_app_mode_t mode;
    contest_app_mode_outcome_t outcome;
    uint32_t mode_result_code = 0U;
    bool done = false;

    if (app == NULL) {
        return CONTEST_APP_STATUS_INVALID_ARGUMENT;
    }
    if (!app->initialized) {
        return CONTEST_APP_STATUS_NOT_INITIALIZED;
    }
    if (contest_app_is_terminal(app->state)) {
        return CONTEST_APP_STATUS_OK;
    }
    if (app->state == CONTEST_APP_STATE_IDLE) {
        return CONTEST_APP_STATUS_NOT_READY;
    }

    if (contest_app_elapsed(
            now_ms,
            app->started_ms,
            app->config.total_timeout_ms)) {
        app->result.reason = CONTEST_APP_REASON_TIMEOUT;
        app->state = CONTEST_APP_STATE_FAULT;
        contest_app_safe_stop(app);
        return CONTEST_APP_STATUS_OK;
    }

    if (app->state == CONTEST_APP_STATE_REPORTING) {
        if (app->ops.poll_report(
                app->ops.user_context,
                &done) != CONTEST_APP_PORT_OK) {
            return contest_app_fault(
                app,
                CONTEST_APP_REASON_REPORT_FAILED
            );
        }
        if (!done) {
            return CONTEST_APP_STATUS_IN_PROGRESS;
        }
        app->state = app->terminal_success
            ? CONTEST_APP_STATE_DONE
            : CONTEST_APP_STATE_FAULT;
        return CONTEST_APP_STATUS_OK;
    }

    if (!contest_app_is_running(app->state)) {
        return contest_app_fault(app, CONTEST_APP_REASON_IO_ERROR);
    }

    mode = contest_app_active_mode(app);
    if (app->ops.service_mode(
            app->ops.user_context,
            mode,
            now_ms) != CONTEST_APP_PORT_OK) {
        return contest_app_fault(app, CONTEST_APP_REASON_IO_ERROR);
    }
    if (app->ops.is_mode_done(
            app->ops.user_context,
            mode,
            &done) != CONTEST_APP_PORT_OK) {
        return contest_app_fault(app, CONTEST_APP_REASON_IO_ERROR);
    }
    if (!done) {
        return CONTEST_APP_STATUS_IN_PROGRESS;
    }
    if (app->ops.get_mode_result(
            app->ops.user_context,
            mode,
            &outcome,
            &mode_result_code) != CONTEST_APP_PORT_OK) {
        return contest_app_fault(app, CONTEST_APP_REASON_IO_ERROR);
    }

    app->result.mode_result_code = mode_result_code;
    if (outcome == CONTEST_APP_MODE_OUTCOME_SUCCESS) {
        app->result.completed_mode = mode;
        app->result.reason = app->result.used_fallback
            ? CONTEST_APP_REASON_FALLBACK_SUCCESS
            : CONTEST_APP_REASON_SUCCESS;
        return contest_app_begin_report(app, true);
    }

    if ((app->state == CONTEST_APP_STATE_RUNNING_ADVANCED)
        && app->config.fallback_on_advanced_failure) {
        contest_app_safe_stop(app);
        app->result.used_fallback = true;
        if (app->ops.start_mode(
                app->ops.user_context,
                CONTEST_APP_MODE_BASIC) != CONTEST_APP_PORT_OK) {
            return contest_app_fault(app, CONTEST_APP_REASON_IO_ERROR);
        }
        app->state = CONTEST_APP_STATE_RUNNING_FALLBACK;
        return CONTEST_APP_STATUS_IN_PROGRESS;
    }

    app->result.completed_mode = mode;
    app->result.reason = CONTEST_APP_REASON_MODE_FAILED;
    return contest_app_begin_report(app, false);
}

bool contest_app_is_done(const contest_app_t *app)
{
    return (app != NULL)
        && app->initialized
        && contest_app_is_terminal(app->state);
}

contest_app_status_t contest_app_get_result(
    const contest_app_t *app,
    contest_app_result_t *result
)
{
    if ((app == NULL) || (result == NULL)) {
        return CONTEST_APP_STATUS_INVALID_ARGUMENT;
    }
    if (!app->initialized) {
        return CONTEST_APP_STATUS_NOT_INITIALIZED;
    }
    if (!contest_app_is_terminal(app->state)) {
        return CONTEST_APP_STATUS_NOT_READY;
    }

    *result = app->result;
    return CONTEST_APP_STATUS_OK;
}

contest_app_status_t contest_app_abort(contest_app_t *app)
{
    if (app == NULL) {
        return CONTEST_APP_STATUS_INVALID_ARGUMENT;
    }
    if (!app->initialized) {
        return CONTEST_APP_STATUS_NOT_INITIALIZED;
    }
    if (contest_app_is_terminal(app->state)) {
        return CONTEST_APP_STATUS_OK;
    }

    app->result.reason = CONTEST_APP_REASON_ABORTED;
    app->state = CONTEST_APP_STATE_ABORTED;
    contest_app_safe_stop(app);
    return CONTEST_APP_STATUS_OK;
}

contest_app_state_t contest_app_get_state(const contest_app_t *app)
{
    if ((app == NULL) || !app->initialized) {
        return CONTEST_APP_STATE_UNINITIALIZED;
    }
    return app->state;
}

void contest_app_deinit(contest_app_t *app)
{
    if (app == NULL) {
        return;
    }
    if (app->initialized
        && (app->state != CONTEST_APP_STATE_IDLE)
        && !contest_app_is_terminal(app->state)) {
        contest_app_safe_stop(app);
    }
    (void)memset(app, 0, sizeof(*app));
}
