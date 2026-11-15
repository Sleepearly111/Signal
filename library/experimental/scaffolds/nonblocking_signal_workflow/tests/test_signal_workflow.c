#include "signal_workflow.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    float plant_value;
    float last_target;
    unsigned start_calls;
    unsigned poll_calls;
    unsigned correction_calls;
    unsigned stop_calls;
    bool fail_start;
    bool invalid_measurement;
    bool saturated;
    bool ignore_correction;
} fake_port_t;

static signal_workflow_port_status_t fake_apply_request(
    void *user_context,
    const signal_workflow_request_t *request
)
{
    fake_port_t *port = (fake_port_t *)user_context;
    port->last_target = request->target;
    return SIGNAL_WORKFLOW_PORT_OK;
}

static signal_workflow_port_status_t fake_start_measurement(
    void *user_context
)
{
    fake_port_t *port = (fake_port_t *)user_context;
    port->start_calls++;
    return port->fail_start
        ? SIGNAL_WORKFLOW_PORT_ERROR
        : SIGNAL_WORKFLOW_PORT_OK;
}

static signal_workflow_port_status_t fake_poll_measurement(
    void *user_context,
    bool *done,
    signal_workflow_measurement_t *measurement
)
{
    fake_port_t *port = (fake_port_t *)user_context;
    port->poll_calls++;
    *done = true;
    measurement->value = port->plant_value;
    measurement->valid = !port->invalid_measurement;
    measurement->saturated = port->saturated;
    return SIGNAL_WORKFLOW_PORT_OK;
}

static signal_workflow_port_status_t fake_compute(
    void *user_context,
    const signal_workflow_request_t *request,
    const signal_workflow_measurement_t *measurement,
    float *correction,
    bool *converged
)
{
    fake_port_t *port = (fake_port_t *)user_context;
    const float error = request->target - measurement->value;
    (void)port;
    *converged = (error > -0.01F) && (error < 0.01F);
    *correction = error;
    return SIGNAL_WORKFLOW_PORT_OK;
}

static signal_workflow_port_status_t fake_apply_correction(
    void *user_context,
    float correction
)
{
    fake_port_t *port = (fake_port_t *)user_context;
    if (!port->ignore_correction) {
        port->plant_value += correction;
    }
    port->correction_calls++;
    return SIGNAL_WORKFLOW_PORT_OK;
}

static void fake_safe_stop(void *user_context)
{
    fake_port_t *port = (fake_port_t *)user_context;
    port->stop_calls++;
}

static signal_workflow_ops_t make_ops(fake_port_t *port)
{
    signal_workflow_ops_t ops;
    (void)memset(&ops, 0, sizeof(ops));
    ops.apply_request = fake_apply_request;
    ops.start_measurement = fake_start_measurement;
    ops.poll_measurement = fake_poll_measurement;
    ops.compute = fake_compute;
    ops.apply_correction = fake_apply_correction;
    ops.safe_stop = fake_safe_stop;
    ops.user_context = port;
    return ops;
}

static signal_workflow_config_t make_config(void)
{
    signal_workflow_config_t config;
    config.settle_ms = 5U;
    config.total_timeout_ms = 100U;
    config.max_iterations = 2U;
    return config;
}

static void test_happy_path(void)
{
    fake_port_t port = {0};
    signal_workflow_t workflow;
    signal_workflow_result_t result;
    signal_workflow_request_t request = {1.0F, 0U};
    signal_workflow_config_t config = make_config();
    signal_workflow_ops_t ops = make_ops(&port);

    port.plant_value = 0.5F;
    assert(signal_workflow_init(&workflow, &config, &ops)
           == SIGNAL_WORKFLOW_STATUS_OK);
    assert(signal_workflow_start(&workflow, &request, 10U)
           == SIGNAL_WORKFLOW_STATUS_OK);
    assert(signal_workflow_service(&workflow, 14U)
           == SIGNAL_WORKFLOW_STATUS_IN_PROGRESS);
    assert(signal_workflow_service(&workflow, 15U)
           == SIGNAL_WORKFLOW_STATUS_IN_PROGRESS);
    assert(signal_workflow_service(&workflow, 16U)
           == SIGNAL_WORKFLOW_STATUS_IN_PROGRESS);
    assert(signal_workflow_service(&workflow, 17U)
           == SIGNAL_WORKFLOW_STATUS_IN_PROGRESS);
    assert(signal_workflow_service(&workflow, 22U)
           == SIGNAL_WORKFLOW_STATUS_IN_PROGRESS);
    assert(signal_workflow_service(&workflow, 23U)
           == SIGNAL_WORKFLOW_STATUS_IN_PROGRESS);
    assert(signal_workflow_service(&workflow, 24U)
           == SIGNAL_WORKFLOW_STATUS_OK);
    assert(signal_workflow_is_done(&workflow));
    assert(signal_workflow_get_result(&workflow, &result)
           == SIGNAL_WORKFLOW_STATUS_OK);
    assert(result.reason == SIGNAL_WORKFLOW_REASON_CONVERGED);
    assert(result.iterations == 1U);
    assert(result.final_value == 1.0F);
    assert(port.correction_calls == 1U);
    assert(port.stop_calls == 1U);
}

static void test_timeout_wrap_safe(void)
{
    fake_port_t port = {0};
    signal_workflow_t workflow;
    signal_workflow_result_t result;
    signal_workflow_request_t request = {1.0F, 0U};
    signal_workflow_config_t config = make_config();
    signal_workflow_ops_t ops = make_ops(&port);
    const uint32_t start = UINT32_MAX - 20U;

    config.settle_ms = 200U;
    config.total_timeout_ms = 30U;
    assert(signal_workflow_init(&workflow, &config, &ops)
           == SIGNAL_WORKFLOW_STATUS_OK);
    assert(signal_workflow_start(&workflow, &request, start)
           == SIGNAL_WORKFLOW_STATUS_OK);
    assert(signal_workflow_service(&workflow, 8U)
           == SIGNAL_WORKFLOW_STATUS_IN_PROGRESS);
    assert(signal_workflow_service(&workflow, 9U)
           == SIGNAL_WORKFLOW_STATUS_OK);
    assert(signal_workflow_get_result(&workflow, &result)
           == SIGNAL_WORKFLOW_STATUS_OK);
    assert(result.reason == SIGNAL_WORKFLOW_REASON_TIMEOUT);
    assert(port.stop_calls == 1U);
}

static void test_fault_and_abort(void)
{
    fake_port_t port = {0};
    signal_workflow_t workflow;
    signal_workflow_result_t result;
    signal_workflow_request_t request = {1.0F, 0U};
    signal_workflow_config_t config = make_config();
    signal_workflow_ops_t ops = make_ops(&port);

    port.fail_start = true;
    assert(signal_workflow_init(&workflow, &config, &ops)
           == SIGNAL_WORKFLOW_STATUS_OK);
    assert(signal_workflow_start(&workflow, &request, 0U)
           == SIGNAL_WORKFLOW_STATUS_OK);
    assert(signal_workflow_service(&workflow, 5U)
           == SIGNAL_WORKFLOW_STATUS_IO_ERROR);
    assert(signal_workflow_get_result(&workflow, &result)
           == SIGNAL_WORKFLOW_STATUS_OK);
    assert(result.reason == SIGNAL_WORKFLOW_REASON_IO_ERROR);

    port.fail_start = false;
    assert(signal_workflow_start(&workflow, &request, 20U)
           == SIGNAL_WORKFLOW_STATUS_OK);
    assert(signal_workflow_abort(&workflow)
           == SIGNAL_WORKFLOW_STATUS_OK);
    assert(signal_workflow_get_result(&workflow, &result)
           == SIGNAL_WORKFLOW_STATUS_OK);
    assert(result.reason == SIGNAL_WORKFLOW_REASON_ABORTED);
    assert(port.stop_calls == 2U);
}

static void test_invalid_and_busy(void)
{
    fake_port_t port = {0};
    signal_workflow_t workflow;
    signal_workflow_request_t request = {1.0F, 0U};
    signal_workflow_config_t config = make_config();
    signal_workflow_ops_t ops = make_ops(&port);

    assert(signal_workflow_init(NULL, &config, &ops)
           == SIGNAL_WORKFLOW_STATUS_INVALID_ARGUMENT);
    config.max_iterations = 0U;
    assert(signal_workflow_init(&workflow, &config, &ops)
           == SIGNAL_WORKFLOW_STATUS_INVALID_ARGUMENT);
    config = make_config();
    ops.safe_stop = NULL;
    assert(signal_workflow_init(&workflow, &config, &ops)
           == SIGNAL_WORKFLOW_STATUS_INVALID_ARGUMENT);
    ops = make_ops(&port);
    assert(signal_workflow_init(&workflow, &config, &ops)
           == SIGNAL_WORKFLOW_STATUS_OK);
    assert(signal_workflow_start(&workflow, &request, 0U)
           == SIGNAL_WORKFLOW_STATUS_OK);
    assert(signal_workflow_start(&workflow, &request, 1U)
           == SIGNAL_WORKFLOW_STATUS_BUSY);
    signal_workflow_deinit(&workflow);
    assert(port.stop_calls == 1U);
}

static void test_measurement_quality_faults(void)
{
    fake_port_t port = {0};
    signal_workflow_t workflow;
    signal_workflow_result_t result;
    signal_workflow_request_t request = {1.0F, 0U};
    signal_workflow_config_t config = make_config();
    signal_workflow_ops_t ops = make_ops(&port);

    port.invalid_measurement = true;
    assert(signal_workflow_init(&workflow, &config, &ops)
           == SIGNAL_WORKFLOW_STATUS_OK);
    assert(signal_workflow_start(&workflow, &request, 0U)
           == SIGNAL_WORKFLOW_STATUS_OK);
    assert(signal_workflow_service(&workflow, 5U)
           == SIGNAL_WORKFLOW_STATUS_IN_PROGRESS);
    assert(signal_workflow_service(&workflow, 6U)
           == SIGNAL_WORKFLOW_STATUS_OK);
    assert(signal_workflow_get_result(&workflow, &result)
           == SIGNAL_WORKFLOW_STATUS_OK);
    assert(result.reason == SIGNAL_WORKFLOW_REASON_INVALID_MEASUREMENT);

    port.invalid_measurement = false;
    port.saturated = true;
    assert(signal_workflow_start(&workflow, &request, 10U)
           == SIGNAL_WORKFLOW_STATUS_OK);
    assert(signal_workflow_service(&workflow, 15U)
           == SIGNAL_WORKFLOW_STATUS_IN_PROGRESS);
    assert(signal_workflow_service(&workflow, 16U)
           == SIGNAL_WORKFLOW_STATUS_OK);
    assert(signal_workflow_get_result(&workflow, &result)
           == SIGNAL_WORKFLOW_STATUS_OK);
    assert(result.reason == SIGNAL_WORKFLOW_REASON_SATURATED);
    assert(port.stop_calls == 2U);
}

static void test_max_iterations(void)
{
    fake_port_t port = {0};
    signal_workflow_t workflow;
    signal_workflow_result_t result;
    signal_workflow_request_t request = {1.0F, 0U};
    signal_workflow_config_t config = make_config();
    signal_workflow_ops_t ops = make_ops(&port);

    config.max_iterations = 1U;
    port.ignore_correction = true;
    assert(signal_workflow_init(&workflow, &config, &ops)
           == SIGNAL_WORKFLOW_STATUS_OK);
    assert(signal_workflow_start(&workflow, &request, 0U)
           == SIGNAL_WORKFLOW_STATUS_OK);
    assert(signal_workflow_service(&workflow, 5U)
           == SIGNAL_WORKFLOW_STATUS_IN_PROGRESS);
    assert(signal_workflow_service(&workflow, 6U)
           == SIGNAL_WORKFLOW_STATUS_IN_PROGRESS);
    assert(signal_workflow_service(&workflow, 7U)
           == SIGNAL_WORKFLOW_STATUS_IN_PROGRESS);
    assert(signal_workflow_service(&workflow, 12U)
           == SIGNAL_WORKFLOW_STATUS_IN_PROGRESS);
    assert(signal_workflow_service(&workflow, 13U)
           == SIGNAL_WORKFLOW_STATUS_IN_PROGRESS);
    assert(signal_workflow_service(&workflow, 14U)
           == SIGNAL_WORKFLOW_STATUS_OK);
    assert(signal_workflow_get_result(&workflow, &result)
           == SIGNAL_WORKFLOW_STATUS_OK);
    assert(result.reason == SIGNAL_WORKFLOW_REASON_MAX_ITERATIONS);
    assert(result.iterations == 1U);
    assert(port.stop_calls == 1U);
}

int main(void)
{
    test_happy_path();
    test_timeout_wrap_safe();
    test_fault_and_abort();
    test_invalid_and_busy();
    test_measurement_quality_faults();
    test_max_iterations();
    (void)puts("signal_workflow tests passed");
    return 0;
}
