#include "regulated_signal_output_app.h"

#include <float.h>
#include <stddef.h>
#include <string.h>

static bool finite_value(float value)
{
    return (value <= FLT_MAX) && (value >= -FLT_MAX);
}

static float clamp_value(float value, float minimum, float maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static bool terminal_state(regulated_output_state_t state)
{
    return (state == REGULATED_OUTPUT_STATE_DONE) ||
           (state == REGULATED_OUTPUT_STATE_FAULT) ||
           (state == REGULATED_OUTPUT_STATE_ABORTED);
}

static regulated_output_reason_t map_reason(
    signal_workflow_reason_t reason
)
{
    switch (reason) {
    case SIGNAL_WORKFLOW_REASON_CONVERGED:
        return REGULATED_OUTPUT_REASON_CONVERGED;
    case SIGNAL_WORKFLOW_REASON_TIMEOUT:
        return REGULATED_OUTPUT_REASON_TIMEOUT;
    case SIGNAL_WORKFLOW_REASON_MAX_ITERATIONS:
        return REGULATED_OUTPUT_REASON_MAX_ITERATIONS;
    case SIGNAL_WORKFLOW_REASON_INVALID_MEASUREMENT:
        return REGULATED_OUTPUT_REASON_INVALID_MEASUREMENT;
    case SIGNAL_WORKFLOW_REASON_SATURATED:
        return REGULATED_OUTPUT_REASON_SATURATED;
    case SIGNAL_WORKFLOW_REASON_IO_ERROR:
        return REGULATED_OUTPUT_REASON_IO_ERROR;
    case SIGNAL_WORKFLOW_REASON_ABORTED:
        return REGULATED_OUTPUT_REASON_ABORTED;
    case SIGNAL_WORKFLOW_REASON_NONE:
    default:
        return REGULATED_OUTPUT_REASON_NONE;
    }
}

static regulated_output_status_t map_status(
    signal_workflow_status_t status
)
{
    switch (status) {
    case SIGNAL_WORKFLOW_STATUS_OK:
        return REGULATED_OUTPUT_STATUS_OK;
    case SIGNAL_WORKFLOW_STATUS_IN_PROGRESS:
        return REGULATED_OUTPUT_STATUS_IN_PROGRESS;
    case SIGNAL_WORKFLOW_STATUS_INVALID_ARGUMENT:
        return REGULATED_OUTPUT_STATUS_INVALID_ARGUMENT;
    case SIGNAL_WORKFLOW_STATUS_NOT_INITIALIZED:
        return REGULATED_OUTPUT_STATUS_NOT_INITIALIZED;
    case SIGNAL_WORKFLOW_STATUS_BUSY:
        return REGULATED_OUTPUT_STATUS_BUSY;
    case SIGNAL_WORKFLOW_STATUS_NOT_READY:
        return REGULATED_OUTPUT_STATUS_NOT_READY;
    case SIGNAL_WORKFLOW_STATUS_IO_ERROR:
    default:
        return REGULATED_OUTPUT_STATUS_IO_ERROR;
    }
}

static signal_workflow_port_status_t workflow_apply_request(
    void *user_context,
    const signal_workflow_request_t *request
)
{
    regulated_output_app_t *app =
        (regulated_output_app_t *)user_context;
    (void)request;

    if (app->ops.configure_signal(
            app->ops.user_context,
            app->request.frequency_hz,
            app->request.waveform) != REGULATED_OUTPUT_PORT_OK) {
        return SIGNAL_WORKFLOW_PORT_ERROR;
    }
    if (app->ops.set_control(
            app->ops.user_context,
            app->current_control) != REGULATED_OUTPUT_PORT_OK) {
        return SIGNAL_WORKFLOW_PORT_ERROR;
    }
    return SIGNAL_WORKFLOW_PORT_OK;
}

static signal_workflow_port_status_t workflow_start_measurement(
    void *user_context
)
{
    regulated_output_app_t *app =
        (regulated_output_app_t *)user_context;
    return (app->ops.start_measurement(app->ops.user_context) ==
            REGULATED_OUTPUT_PORT_OK)
        ? SIGNAL_WORKFLOW_PORT_OK
        : SIGNAL_WORKFLOW_PORT_ERROR;
}

static signal_workflow_port_status_t workflow_poll_measurement(
    void *user_context,
    bool *done,
    signal_workflow_measurement_t *measurement
)
{
    regulated_output_app_t *app =
        (regulated_output_app_t *)user_context;
    bool valid = false;

    if (app->ops.poll_measurement(
            app->ops.user_context,
            done,
            &measurement->value,
            &valid,
            &measurement->saturated) != REGULATED_OUTPUT_PORT_OK) {
        return SIGNAL_WORKFLOW_PORT_ERROR;
    }
    measurement->valid =
        valid &&
        finite_value(measurement->value) &&
        (measurement->value >= app->config.minimum_valid_vpp);
    return SIGNAL_WORKFLOW_PORT_OK;
}

static signal_workflow_port_status_t workflow_compute(
    void *user_context,
    const signal_workflow_request_t *request,
    const signal_workflow_measurement_t *measurement,
    float *correction,
    bool *converged
)
{
    regulated_output_app_t *app =
        (regulated_output_app_t *)user_context;
    float error;
    float tolerance;
    float ratio;

    error = request->target - measurement->value;
    if (error < 0.0F) {
        error = -error;
    }
    tolerance = request->target * app->config.relative_tolerance;
    if (tolerance < app->config.absolute_tolerance_vpp) {
        tolerance = app->config.absolute_tolerance_vpp;
    }
    *converged = error <= tolerance;
    if (*converged) {
        *correction = 1.0F;
        return SIGNAL_WORKFLOW_PORT_OK;
    }

    ratio = request->target / measurement->value;
    *correction = clamp_value(
        ratio,
        app->config.minimum_correction_ratio,
        app->config.maximum_correction_ratio
    );
    return SIGNAL_WORKFLOW_PORT_OK;
}

static signal_workflow_port_status_t workflow_apply_correction(
    void *user_context,
    float correction
)
{
    regulated_output_app_t *app =
        (regulated_output_app_t *)user_context;
    const float next_control = clamp_value(
        app->current_control * correction,
        app->config.minimum_control,
        app->config.maximum_control
    );

    if (app->ops.set_control(
            app->ops.user_context,
            next_control) != REGULATED_OUTPUT_PORT_OK) {
        return SIGNAL_WORKFLOW_PORT_ERROR;
    }
    app->current_control = next_control;
    return SIGNAL_WORKFLOW_PORT_OK;
}

static void workflow_terminal(void *user_context)
{
    regulated_output_app_t *app =
        (regulated_output_app_t *)user_context;

    if (signal_workflow_get_state(&app->workflow) !=
        SIGNAL_WORKFLOW_STATE_DONE) {
        app->ops.safe_stop(app->ops.user_context);
    }
}

static void sync_result(regulated_output_app_t *app)
{
    signal_workflow_result_t result;
    const signal_workflow_state_t workflow_state =
        signal_workflow_get_state(&app->workflow);

    if (signal_workflow_get_result(&app->workflow, &result) !=
        SIGNAL_WORKFLOW_STATUS_OK) {
        return;
    }
    app->result.final_vpp = result.final_value;
    app->result.final_control = app->current_control;
    app->result.iterations = result.iterations;
    app->result.reason = map_reason(result.reason);

    if (workflow_state == SIGNAL_WORKFLOW_STATE_DONE) {
        app->state = REGULATED_OUTPUT_STATE_DONE;
    } else if (workflow_state == SIGNAL_WORKFLOW_STATE_ABORTED) {
        app->state = REGULATED_OUTPUT_STATE_ABORTED;
    } else {
        app->state = REGULATED_OUTPUT_STATE_FAULT;
    }
}

regulated_output_status_t regulated_output_app_init(
    regulated_output_app_t *app,
    const regulated_output_config_t *config,
    const regulated_output_ops_t *ops
)
{
    signal_workflow_config_t workflow_config;
    signal_workflow_ops_t workflow_ops;

    if ((app == NULL) || (config == NULL) || (ops == NULL) ||
        (ops->configure_signal == NULL) ||
        (ops->set_control == NULL) ||
        (ops->start_measurement == NULL) ||
        (ops->poll_measurement == NULL) ||
        (ops->safe_stop == NULL) ||
        (config->total_timeout_ms == 0U) ||
        (config->max_iterations == 0U) ||
        !finite_value(config->minimum_control) ||
        !finite_value(config->maximum_control) ||
        (config->minimum_control < 0.0F) ||
        (config->minimum_control >= config->maximum_control) ||
        !finite_value(config->minimum_valid_vpp) ||
        (config->minimum_valid_vpp <= 0.0F) ||
        !finite_value(config->absolute_tolerance_vpp) ||
        (config->absolute_tolerance_vpp < 0.0F) ||
        !finite_value(config->relative_tolerance) ||
        (config->relative_tolerance < 0.0F) ||
        ((config->absolute_tolerance_vpp == 0.0F) &&
         (config->relative_tolerance == 0.0F)) ||
        !finite_value(config->minimum_correction_ratio) ||
        !finite_value(config->maximum_correction_ratio) ||
        (config->minimum_correction_ratio <= 0.0F) ||
        (config->minimum_correction_ratio > 1.0F) ||
        (config->maximum_correction_ratio < 1.0F) ||
        (config->minimum_correction_ratio >=
         config->maximum_correction_ratio)) {
        return REGULATED_OUTPUT_STATUS_INVALID_ARGUMENT;
    }

    (void)memset(app, 0, sizeof(*app));
    app->config = *config;
    app->ops = *ops;

    workflow_config.settle_ms = config->settle_ms;
    workflow_config.total_timeout_ms = config->total_timeout_ms;
    workflow_config.max_iterations = config->max_iterations;
    workflow_ops.apply_request = workflow_apply_request;
    workflow_ops.start_measurement = workflow_start_measurement;
    workflow_ops.poll_measurement = workflow_poll_measurement;
    workflow_ops.compute = workflow_compute;
    workflow_ops.apply_correction = workflow_apply_correction;
    workflow_ops.safe_stop = workflow_terminal;
    workflow_ops.user_context = app;
    if (signal_workflow_init(
            &app->workflow,
            &workflow_config,
            &workflow_ops) != SIGNAL_WORKFLOW_STATUS_OK) {
        return REGULATED_OUTPUT_STATUS_INVALID_ARGUMENT;
    }

    app->state = REGULATED_OUTPUT_STATE_IDLE;
    app->initialized = true;
    return REGULATED_OUTPUT_STATUS_OK;
}

regulated_output_status_t regulated_output_app_start(
    regulated_output_app_t *app,
    const regulated_output_request_t *request,
    uint32_t now_ms
)
{
    signal_workflow_request_t workflow_request;
    signal_workflow_status_t status;

    if ((app == NULL) || !app->initialized) {
        return REGULATED_OUTPUT_STATUS_NOT_INITIALIZED;
    }
    if (app->state == REGULATED_OUTPUT_STATE_RUNNING) {
        return REGULATED_OUTPUT_STATUS_BUSY;
    }
    if ((request == NULL) ||
        !finite_value(request->frequency_hz) ||
        (request->frequency_hz <= 0.0F) ||
        !finite_value(request->target_vpp) ||
        (request->target_vpp < app->config.minimum_valid_vpp) ||
        !finite_value(request->initial_control) ||
        (request->initial_control < app->config.minimum_control) ||
        (request->initial_control > app->config.maximum_control) ||
        (request->waveform > REGULATED_OUTPUT_WAVEFORM_TRIANGLE)) {
        return REGULATED_OUTPUT_STATUS_INVALID_ARGUMENT;
    }

    app->request = *request;
    app->current_control = request->initial_control;
    (void)memset(&app->result, 0, sizeof(app->result));
    workflow_request.target = request->target_vpp;
    workflow_request.mode = (uint32_t)request->waveform;
    status = signal_workflow_start(
        &app->workflow,
        &workflow_request,
        now_ms
    );
    if (status == SIGNAL_WORKFLOW_STATUS_OK) {
        app->state = REGULATED_OUTPUT_STATE_RUNNING;
        return REGULATED_OUTPUT_STATUS_IN_PROGRESS;
    }
    if (signal_workflow_is_done(&app->workflow)) {
        sync_result(app);
    }
    return map_status(status);
}

regulated_output_status_t regulated_output_app_service(
    regulated_output_app_t *app,
    uint32_t now_ms
)
{
    signal_workflow_status_t status;

    if ((app == NULL) || !app->initialized) {
        return REGULATED_OUTPUT_STATUS_NOT_INITIALIZED;
    }
    if (app->state != REGULATED_OUTPUT_STATE_RUNNING) {
        return terminal_state(app->state)
            ? REGULATED_OUTPUT_STATUS_OK
            : REGULATED_OUTPUT_STATUS_NOT_READY;
    }

    status = signal_workflow_service(&app->workflow, now_ms);
    if (signal_workflow_is_done(&app->workflow)) {
        sync_result(app);
        return map_status(status);
    }
    return (status == SIGNAL_WORKFLOW_STATUS_OK)
        ? REGULATED_OUTPUT_STATUS_IN_PROGRESS
        : map_status(status);
}

bool regulated_output_app_is_done(const regulated_output_app_t *app)
{
    return (app != NULL) && terminal_state(app->state);
}

regulated_output_status_t regulated_output_app_get_result(
    const regulated_output_app_t *app,
    regulated_output_result_t *result
)
{
    if ((app == NULL) || !app->initialized) {
        return REGULATED_OUTPUT_STATUS_NOT_INITIALIZED;
    }
    if (result == NULL) {
        return REGULATED_OUTPUT_STATUS_INVALID_ARGUMENT;
    }
    if (!terminal_state(app->state)) {
        return REGULATED_OUTPUT_STATUS_NOT_READY;
    }
    *result = app->result;
    return REGULATED_OUTPUT_STATUS_OK;
}

regulated_output_status_t regulated_output_app_abort(
    regulated_output_app_t *app
)
{
    const signal_workflow_status_t status =
        (app != NULL) && app->initialized
        ? signal_workflow_abort(&app->workflow)
        : SIGNAL_WORKFLOW_STATUS_NOT_INITIALIZED;

    if (status == SIGNAL_WORKFLOW_STATUS_OK) {
        sync_result(app);
    }
    return map_status(status);
}

regulated_output_state_t regulated_output_app_get_state(
    const regulated_output_app_t *app
)
{
    return (app != NULL)
        ? app->state
        : REGULATED_OUTPUT_STATE_UNINITIALIZED;
}

void regulated_output_app_deinit(regulated_output_app_t *app)
{
    if (app == NULL) {
        return;
    }
    signal_workflow_deinit(&app->workflow);
    (void)memset(app, 0, sizeof(*app));
}
