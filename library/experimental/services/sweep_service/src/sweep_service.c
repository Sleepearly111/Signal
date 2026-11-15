#include "sweep_service.h"

#include <float.h>
#include <string.h>

static bool elapsed(uint32_t now_ms, uint32_t start_ms, uint32_t limit_ms)
{
    return (uint32_t)(now_ms - start_ms) >= limit_ms;
}

static bool valid_frequency(float value)
{
    return (value > 0.0F) && (value <= FLT_MAX);
}

static bool active_state(sweep_state_t state)
{
    return (state == SWEEP_STATE_SETTLING) ||
           (state == SWEEP_STATE_MEASURING);
}

static sweep_status_t finish(
    sweep_service_t *service,
    sweep_state_t state,
    sweep_reason_t reason,
    sweep_status_t status
)
{
    service->state = state;
    service->result.reason = reason;
    service->ops.safe_stop(service->ops.user_context);
    return status;
}

static sweep_status_t begin_point(sweep_service_t *service, uint32_t now_ms)
{
    const size_t index = service->result.completed_points;
    const float frequency = service->request.frequencies_hz[index];

    if (service->ops.set_frequency(service->ops.user_context, frequency)
        != SWEEP_PORT_OK) {
        service->result.failed_index = index;
        return finish(
            service,
            SWEEP_STATE_FAULT,
            SWEEP_REASON_IO_ERROR,
            SWEEP_STATUS_IO_ERROR
        );
    }

    service->state = SWEEP_STATE_SETTLING;
    service->state_started_ms = now_ms;
    return SWEEP_STATUS_IN_PROGRESS;
}

sweep_status_t sweep_service_init(
    sweep_service_t *service,
    const sweep_config_t *config,
    const sweep_ops_t *ops
)
{
    if ((service == NULL) || (config == NULL) || (ops == NULL) ||
        (ops->set_frequency == NULL) ||
        (ops->start_measurement == NULL) ||
        (ops->poll_measurement == NULL) ||
        (ops->safe_stop == NULL) ||
        (config->point_timeout_ms == 0U) ||
        (config->total_timeout_ms == 0U)) {
        return SWEEP_STATUS_INVALID_ARGUMENT;
    }

    (void)memset(service, 0, sizeof(*service));
    service->ops = *ops;
    service->config = *config;
    service->state = SWEEP_STATE_IDLE;
    service->initialized = true;
    return SWEEP_STATUS_OK;
}

sweep_status_t sweep_service_start(
    sweep_service_t *service,
    const sweep_request_t *request,
    uint32_t now_ms
)
{
    size_t index;

    if ((service == NULL) || !service->initialized) {
        return SWEEP_STATUS_NOT_INITIALIZED;
    }
    if (active_state(service->state)) {
        return SWEEP_STATUS_BUSY;
    }
    if ((request == NULL) || (request->frequencies_hz == NULL) ||
        (request->output_points == NULL) || (request->point_count == 0U) ||
        (request->output_capacity < request->point_count)) {
        return SWEEP_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0U; index < request->point_count; ++index) {
        if (!valid_frequency(request->frequencies_hz[index]) ||
            ((index > 0U) &&
             (request->frequencies_hz[index] <=
              request->frequencies_hz[index - 1U]))) {
            return SWEEP_STATUS_INVALID_ARGUMENT;
        }
    }

    service->request = *request;
    (void)memset(&service->result, 0, sizeof(service->result));
    service->result.failed_index = request->point_count;
    service->retry_count = 0U;
    service->started_ms = now_ms;
    return begin_point(service, now_ms);
}

sweep_status_t sweep_service_service(
    sweep_service_t *service,
    uint32_t now_ms
)
{
    bool done = false;
    sweep_point_t point;
    const size_t index =
        (service != NULL) ? service->result.completed_points : 0U;

    if ((service == NULL) || !service->initialized) {
        return SWEEP_STATUS_NOT_INITIALIZED;
    }
    if (!active_state(service->state)) {
        return sweep_service_is_done(service)
            ? SWEEP_STATUS_OK
            : SWEEP_STATUS_NOT_READY;
    }
    if (elapsed(
            now_ms,
            service->started_ms,
            service->config.total_timeout_ms
        )) {
        service->result.failed_index = index;
        return finish(
            service,
            SWEEP_STATE_FAULT,
            SWEEP_REASON_TOTAL_TIMEOUT,
            SWEEP_STATUS_OK
        );
    }

    if (service->state == SWEEP_STATE_SETTLING) {
        if (!elapsed(
                now_ms,
                service->state_started_ms,
                service->config.settle_ms
            )) {
            return SWEEP_STATUS_IN_PROGRESS;
        }
        if (service->ops.start_measurement(service->ops.user_context)
            != SWEEP_PORT_OK) {
            service->result.failed_index = index;
            return finish(
                service,
                SWEEP_STATE_FAULT,
                SWEEP_REASON_IO_ERROR,
                SWEEP_STATUS_IO_ERROR
            );
        }
        service->state = SWEEP_STATE_MEASURING;
        service->state_started_ms = now_ms;
        return SWEEP_STATUS_IN_PROGRESS;
    }

    if (elapsed(
            now_ms,
            service->state_started_ms,
            service->config.point_timeout_ms
        )) {
        if (service->retry_count < service->config.max_retries_per_point) {
            service->retry_count++;
            service->result.retries_used++;
            return begin_point(service, now_ms);
        }
        service->result.failed_index = index;
        return finish(
            service,
            SWEEP_STATE_FAULT,
            SWEEP_REASON_POINT_TIMEOUT,
            SWEEP_STATUS_OK
        );
    }

    (void)memset(&point, 0, sizeof(point));
    if (service->ops.poll_measurement(
            service->ops.user_context,
            &done,
            &point
        ) != SWEEP_PORT_OK) {
        service->result.failed_index = index;
        return finish(
            service,
            SWEEP_STATE_FAULT,
            SWEEP_REASON_IO_ERROR,
            SWEEP_STATUS_IO_ERROR
        );
    }
    if (!done) {
        return SWEEP_STATUS_IN_PROGRESS;
    }
    if (!point.valid || point.saturated ||
        (point.input_value <= 0.0F) || (point.output_value < 0.0F)) {
        if (service->retry_count < service->config.max_retries_per_point) {
            service->retry_count++;
            service->result.retries_used++;
            return begin_point(service, now_ms);
        }
        service->result.failed_index = index;
        return finish(
            service,
            SWEEP_STATE_FAULT,
            SWEEP_REASON_INVALID_MEASUREMENT,
            SWEEP_STATUS_OK
        );
    }

    point.frequency_hz = service->request.frequencies_hz[index];
    service->request.output_points[index] = point;
    service->result.completed_points++;
    service->retry_count = 0U;
    if (service->result.completed_points == service->request.point_count) {
        return finish(
            service,
            SWEEP_STATE_DONE,
            SWEEP_REASON_COMPLETED,
            SWEEP_STATUS_OK
        );
    }
    return begin_point(service, now_ms);
}

bool sweep_service_is_done(const sweep_service_t *service)
{
    return (service != NULL) &&
           ((service->state == SWEEP_STATE_DONE) ||
            (service->state == SWEEP_STATE_FAULT) ||
            (service->state == SWEEP_STATE_ABORTED));
}

sweep_status_t sweep_service_get_result(
    const sweep_service_t *service,
    sweep_result_t *result
)
{
    if ((service == NULL) || !service->initialized) {
        return SWEEP_STATUS_NOT_INITIALIZED;
    }
    if (result == NULL) {
        return SWEEP_STATUS_INVALID_ARGUMENT;
    }
    if (!sweep_service_is_done(service)) {
        return SWEEP_STATUS_NOT_READY;
    }
    *result = service->result;
    return SWEEP_STATUS_OK;
}

sweep_status_t sweep_service_abort(sweep_service_t *service)
{
    if ((service == NULL) || !service->initialized) {
        return SWEEP_STATUS_NOT_INITIALIZED;
    }
    if (!active_state(service->state)) {
        return SWEEP_STATUS_NOT_READY;
    }
    return finish(
        service,
        SWEEP_STATE_ABORTED,
        SWEEP_REASON_ABORTED,
        SWEEP_STATUS_OK
    );
}

sweep_state_t sweep_service_get_state(const sweep_service_t *service)
{
    return (service != NULL) ? service->state : SWEEP_STATE_UNINITIALIZED;
}

void sweep_service_deinit(sweep_service_t *service)
{
    if (service == NULL) {
        return;
    }
    if (service->initialized && active_state(service->state)) {
        service->ops.safe_stop(service->ops.user_context);
    }
    (void)memset(service, 0, sizeof(*service));
}
