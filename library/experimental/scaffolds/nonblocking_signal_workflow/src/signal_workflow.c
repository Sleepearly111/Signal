#include "signal_workflow.h"

#include <stddef.h>
#include <string.h>

static bool signal_workflow_elapsed(
    uint32_t now_ms,
    uint32_t start_ms,
    uint32_t duration_ms
)
{
    return (uint32_t)(now_ms - start_ms) >= duration_ms;
}

static bool signal_workflow_is_terminal(signal_workflow_state_t state)
{
    return (state == SIGNAL_WORKFLOW_STATE_DONE)
        || (state == SIGNAL_WORKFLOW_STATE_FAULT)
        || (state == SIGNAL_WORKFLOW_STATE_ABORTED);
}

static void signal_workflow_stop(signal_workflow_t *workflow)
{
    if (workflow->ops.safe_stop != NULL) {
        workflow->ops.safe_stop(workflow->ops.user_context);
    }
}

static signal_workflow_status_t signal_workflow_fail(
    signal_workflow_t *workflow,
    signal_workflow_reason_t reason
)
{
    workflow->result.reason = reason;
    workflow->state = SIGNAL_WORKFLOW_STATE_FAULT;
    signal_workflow_stop(workflow);
    return SIGNAL_WORKFLOW_STATUS_IO_ERROR;
}

signal_workflow_status_t signal_workflow_init(
    signal_workflow_t *workflow,
    const signal_workflow_config_t *config,
    const signal_workflow_ops_t *ops
)
{
    if ((workflow == NULL) || (config == NULL) || (ops == NULL)) {
        return SIGNAL_WORKFLOW_STATUS_INVALID_ARGUMENT;
    }

    if ((config->total_timeout_ms == 0U)
        || (config->max_iterations == 0U)
        || (ops->apply_request == NULL)
        || (ops->start_measurement == NULL)
        || (ops->poll_measurement == NULL)
        || (ops->compute == NULL)
        || (ops->apply_correction == NULL)
        || (ops->safe_stop == NULL)) {
        return SIGNAL_WORKFLOW_STATUS_INVALID_ARGUMENT;
    }

    (void)memset(workflow, 0, sizeof(*workflow));
    workflow->ops = *ops;
    workflow->config = *config;
    workflow->state = SIGNAL_WORKFLOW_STATE_IDLE;
    workflow->initialized = true;

    return SIGNAL_WORKFLOW_STATUS_OK;
}

signal_workflow_status_t signal_workflow_start(
    signal_workflow_t *workflow,
    const signal_workflow_request_t *request,
    uint32_t now_ms
)
{
    if ((workflow == NULL) || (request == NULL)) {
        return SIGNAL_WORKFLOW_STATUS_INVALID_ARGUMENT;
    }
    if (!workflow->initialized) {
        return SIGNAL_WORKFLOW_STATUS_NOT_INITIALIZED;
    }
    if ((workflow->state != SIGNAL_WORKFLOW_STATE_IDLE)
        && !signal_workflow_is_terminal(workflow->state)) {
        return SIGNAL_WORKFLOW_STATUS_BUSY;
    }

    workflow->request = *request;
    (void)memset(&workflow->measurement, 0, sizeof(workflow->measurement));
    (void)memset(&workflow->result, 0, sizeof(workflow->result));
    workflow->started_ms = now_ms;
    workflow->state_started_ms = now_ms;

    if (workflow->ops.apply_request(
            workflow->ops.user_context,
            &workflow->request) != SIGNAL_WORKFLOW_PORT_OK) {
        return signal_workflow_fail(
            workflow,
            SIGNAL_WORKFLOW_REASON_IO_ERROR
        );
    }

    workflow->state = SIGNAL_WORKFLOW_STATE_SETTLING;
    return SIGNAL_WORKFLOW_STATUS_OK;
}

signal_workflow_status_t signal_workflow_service(
    signal_workflow_t *workflow,
    uint32_t now_ms
)
{
    bool done = false;
    bool converged = false;
    float correction = 0.0F;

    if (workflow == NULL) {
        return SIGNAL_WORKFLOW_STATUS_INVALID_ARGUMENT;
    }
    if (!workflow->initialized) {
        return SIGNAL_WORKFLOW_STATUS_NOT_INITIALIZED;
    }
    if (signal_workflow_is_terminal(workflow->state)) {
        return SIGNAL_WORKFLOW_STATUS_OK;
    }
    if (workflow->state == SIGNAL_WORKFLOW_STATE_IDLE) {
        return SIGNAL_WORKFLOW_STATUS_NOT_READY;
    }

    if (signal_workflow_elapsed(
            now_ms,
            workflow->started_ms,
            workflow->config.total_timeout_ms)) {
        workflow->result.reason = SIGNAL_WORKFLOW_REASON_TIMEOUT;
        workflow->state = SIGNAL_WORKFLOW_STATE_FAULT;
        signal_workflow_stop(workflow);
        return SIGNAL_WORKFLOW_STATUS_OK;
    }

    switch (workflow->state) {
    case SIGNAL_WORKFLOW_STATE_SETTLING:
        if (!signal_workflow_elapsed(
                now_ms,
                workflow->state_started_ms,
                workflow->config.settle_ms)) {
            return SIGNAL_WORKFLOW_STATUS_IN_PROGRESS;
        }
        if (workflow->ops.start_measurement(
                workflow->ops.user_context) != SIGNAL_WORKFLOW_PORT_OK) {
            return signal_workflow_fail(
                workflow,
                SIGNAL_WORKFLOW_REASON_IO_ERROR
            );
        }
        workflow->state = SIGNAL_WORKFLOW_STATE_MEASURING;
        workflow->state_started_ms = now_ms;
        return SIGNAL_WORKFLOW_STATUS_IN_PROGRESS;

    case SIGNAL_WORKFLOW_STATE_MEASURING:
        if (workflow->ops.poll_measurement(
                workflow->ops.user_context,
                &done,
                &workflow->measurement) != SIGNAL_WORKFLOW_PORT_OK) {
            return signal_workflow_fail(
                workflow,
                SIGNAL_WORKFLOW_REASON_IO_ERROR
            );
        }
        if (!done) {
            return SIGNAL_WORKFLOW_STATUS_IN_PROGRESS;
        }
        workflow->result.final_value = workflow->measurement.value;
        if (!workflow->measurement.valid) {
            workflow->result.reason =
                SIGNAL_WORKFLOW_REASON_INVALID_MEASUREMENT;
            workflow->state = SIGNAL_WORKFLOW_STATE_FAULT;
            signal_workflow_stop(workflow);
            return SIGNAL_WORKFLOW_STATUS_OK;
        }
        if (workflow->measurement.saturated) {
            workflow->result.reason = SIGNAL_WORKFLOW_REASON_SATURATED;
            workflow->state = SIGNAL_WORKFLOW_STATE_FAULT;
            signal_workflow_stop(workflow);
            return SIGNAL_WORKFLOW_STATUS_OK;
        }
        workflow->state = SIGNAL_WORKFLOW_STATE_COMPUTING;
        return SIGNAL_WORKFLOW_STATUS_IN_PROGRESS;

    case SIGNAL_WORKFLOW_STATE_COMPUTING:
        if (workflow->ops.compute(
                workflow->ops.user_context,
                &workflow->request,
                &workflow->measurement,
                &correction,
                &converged) != SIGNAL_WORKFLOW_PORT_OK) {
            return signal_workflow_fail(
                workflow,
                SIGNAL_WORKFLOW_REASON_IO_ERROR
            );
        }
        if (converged) {
            workflow->result.reason = SIGNAL_WORKFLOW_REASON_CONVERGED;
            workflow->state = SIGNAL_WORKFLOW_STATE_DONE;
            signal_workflow_stop(workflow);
            return SIGNAL_WORKFLOW_STATUS_OK;
        }
        if (workflow->result.iterations >= workflow->config.max_iterations) {
            workflow->result.reason =
                SIGNAL_WORKFLOW_REASON_MAX_ITERATIONS;
            workflow->state = SIGNAL_WORKFLOW_STATE_FAULT;
            signal_workflow_stop(workflow);
            return SIGNAL_WORKFLOW_STATUS_OK;
        }
        if (workflow->ops.apply_correction(
                workflow->ops.user_context,
                correction) != SIGNAL_WORKFLOW_PORT_OK) {
            return signal_workflow_fail(
                workflow,
                SIGNAL_WORKFLOW_REASON_IO_ERROR
            );
        }
        workflow->result.iterations++;
        workflow->state = SIGNAL_WORKFLOW_STATE_SETTLING;
        workflow->state_started_ms = now_ms;
        return SIGNAL_WORKFLOW_STATUS_IN_PROGRESS;

    default:
        return signal_workflow_fail(
            workflow,
            SIGNAL_WORKFLOW_REASON_IO_ERROR
        );
    }
}

bool signal_workflow_is_done(const signal_workflow_t *workflow)
{
    return (workflow != NULL)
        && workflow->initialized
        && signal_workflow_is_terminal(workflow->state);
}

signal_workflow_status_t signal_workflow_get_result(
    const signal_workflow_t *workflow,
    signal_workflow_result_t *result
)
{
    if ((workflow == NULL) || (result == NULL)) {
        return SIGNAL_WORKFLOW_STATUS_INVALID_ARGUMENT;
    }
    if (!workflow->initialized) {
        return SIGNAL_WORKFLOW_STATUS_NOT_INITIALIZED;
    }
    if (!signal_workflow_is_terminal(workflow->state)) {
        return SIGNAL_WORKFLOW_STATUS_NOT_READY;
    }

    *result = workflow->result;
    return SIGNAL_WORKFLOW_STATUS_OK;
}

signal_workflow_status_t signal_workflow_abort(signal_workflow_t *workflow)
{
    if (workflow == NULL) {
        return SIGNAL_WORKFLOW_STATUS_INVALID_ARGUMENT;
    }
    if (!workflow->initialized) {
        return SIGNAL_WORKFLOW_STATUS_NOT_INITIALIZED;
    }
    if (signal_workflow_is_terminal(workflow->state)) {
        return SIGNAL_WORKFLOW_STATUS_OK;
    }

    workflow->result.reason = SIGNAL_WORKFLOW_REASON_ABORTED;
    workflow->state = SIGNAL_WORKFLOW_STATE_ABORTED;
    signal_workflow_stop(workflow);
    return SIGNAL_WORKFLOW_STATUS_OK;
}

signal_workflow_state_t signal_workflow_get_state(
    const signal_workflow_t *workflow
)
{
    if ((workflow == NULL) || !workflow->initialized) {
        return SIGNAL_WORKFLOW_STATE_UNINITIALIZED;
    }
    return workflow->state;
}

void signal_workflow_deinit(signal_workflow_t *workflow)
{
    if (workflow == NULL) {
        return;
    }

    if (workflow->initialized
        && !signal_workflow_is_terminal(workflow->state)
        && (workflow->state != SIGNAL_WORKFLOW_STATE_IDLE)) {
        signal_workflow_stop(workflow);
    }
    (void)memset(workflow, 0, sizeof(*workflow));
}
