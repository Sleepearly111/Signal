#include "sweep_service.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    float frequency_hz;
    unsigned set_calls;
    unsigned start_calls;
    unsigned poll_calls;
    unsigned stop_calls;
    unsigned invalid_polls;
    bool never_done;
    bool fail_set;
} fake_port_t;

static sweep_port_status_t fake_set(void *context, float frequency_hz)
{
    fake_port_t *port = (fake_port_t *)context;
    port->set_calls++;
    port->frequency_hz = frequency_hz;
    return port->fail_set ? SWEEP_PORT_ERROR : SWEEP_PORT_OK;
}

static sweep_port_status_t fake_start(void *context)
{
    fake_port_t *port = (fake_port_t *)context;
    port->start_calls++;
    return SWEEP_PORT_OK;
}

static sweep_port_status_t fake_poll(
    void *context,
    bool *done,
    sweep_point_t *point
)
{
    fake_port_t *port = (fake_port_t *)context;
    port->poll_calls++;
    if (port->never_done) {
        *done = false;
        return SWEEP_PORT_OK;
    }
    *done = true;
    point->input_value = 1.0F;
    point->output_value = port->frequency_hz / 1000.0F;
    point->valid = port->invalid_polls == 0U;
    point->saturated = false;
    if (port->invalid_polls > 0U) {
        port->invalid_polls--;
    }
    return SWEEP_PORT_OK;
}

static void fake_stop(void *context)
{
    fake_port_t *port = (fake_port_t *)context;
    port->stop_calls++;
}

static sweep_ops_t make_ops(fake_port_t *port)
{
    sweep_ops_t ops;
    (void)memset(&ops, 0, sizeof(ops));
    ops.set_frequency = fake_set;
    ops.start_measurement = fake_start;
    ops.poll_measurement = fake_poll;
    ops.safe_stop = fake_stop;
    ops.user_context = port;
    return ops;
}

static sweep_config_t make_config(void)
{
    sweep_config_t config = {2U, 5U, 100U, 1U};
    return config;
}

static sweep_status_t run_one_point(
    sweep_service_t *service,
    uint32_t start_ms
)
{
    assert(sweep_service_service(service, start_ms + 2U)
           == SWEEP_STATUS_IN_PROGRESS);
    return sweep_service_service(service, start_ms + 3U);
}

static void test_happy_path_and_order(void)
{
    const float frequencies[] = {500.0F, 1000.0F, 2000.0F};
    sweep_point_t points[3];
    fake_port_t port = {0};
    sweep_service_t service;
    sweep_result_t result;
    sweep_config_t config = make_config();
    sweep_ops_t ops = make_ops(&port);
    sweep_request_t request = {frequencies, 3U, points, 3U};

    assert(sweep_service_init(&service, &config, &ops) == SWEEP_STATUS_OK);
    assert(sweep_service_start(&service, &request, 0U)
           == SWEEP_STATUS_IN_PROGRESS);
    assert(run_one_point(&service, 0U) == SWEEP_STATUS_IN_PROGRESS);
    assert(run_one_point(&service, 3U) == SWEEP_STATUS_IN_PROGRESS);
    assert(run_one_point(&service, 6U) == SWEEP_STATUS_OK);
    assert(sweep_service_is_done(&service));
    assert(sweep_service_get_result(&service, &result) == SWEEP_STATUS_OK);
    assert(result.reason == SWEEP_REASON_COMPLETED);
    assert(result.completed_points == 3U);
    assert(points[0].frequency_hz == 500.0F);
    assert(points[2].output_value == 2.0F);
    assert(port.stop_calls == 1U);
}

static void test_retry_then_success(void)
{
    const float frequencies[] = {1000.0F};
    sweep_point_t point;
    fake_port_t port = {0};
    sweep_service_t service;
    sweep_result_t result;
    sweep_config_t config = make_config();
    sweep_ops_t ops = make_ops(&port);
    sweep_request_t request = {frequencies, 1U, &point, 1U};

    port.invalid_polls = 1U;
    assert(sweep_service_init(&service, &config, &ops) == SWEEP_STATUS_OK);
    assert(sweep_service_start(&service, &request, 10U)
           == SWEEP_STATUS_IN_PROGRESS);
    assert(run_one_point(&service, 10U) == SWEEP_STATUS_IN_PROGRESS);
    assert(run_one_point(&service, 13U) == SWEEP_STATUS_OK);
    assert(sweep_service_get_result(&service, &result) == SWEEP_STATUS_OK);
    assert(result.reason == SWEEP_REASON_COMPLETED);
    assert(result.retries_used == 1U);
}

static void test_point_and_total_timeout(void)
{
    const float frequencies[] = {1000.0F};
    sweep_point_t point;
    fake_port_t port = {0};
    sweep_service_t service;
    sweep_result_t result;
    sweep_config_t config = make_config();
    sweep_ops_t ops = make_ops(&port);
    sweep_request_t request = {frequencies, 1U, &point, 1U};

    port.never_done = true;
    config.max_retries_per_point = 0U;
    assert(sweep_service_init(&service, &config, &ops) == SWEEP_STATUS_OK);
    assert(sweep_service_start(&service, &request, UINT32_MAX - 2U)
           == SWEEP_STATUS_IN_PROGRESS);
    assert(sweep_service_service(&service, 0U) == SWEEP_STATUS_IN_PROGRESS);
    assert(sweep_service_service(&service, 5U) == SWEEP_STATUS_OK);
    assert(sweep_service_get_result(&service, &result) == SWEEP_STATUS_OK);
    assert(result.reason == SWEEP_REASON_POINT_TIMEOUT);

    config.point_timeout_ms = 100U;
    config.total_timeout_ms = 4U;
    assert(sweep_service_init(&service, &config, &ops) == SWEEP_STATUS_OK);
    assert(sweep_service_start(&service, &request, 20U)
           == SWEEP_STATUS_IN_PROGRESS);
    assert(sweep_service_service(&service, 24U) == SWEEP_STATUS_OK);
    assert(sweep_service_get_result(&service, &result) == SWEEP_STATUS_OK);
    assert(result.reason == SWEEP_REASON_TOTAL_TIMEOUT);
}

static void test_invalid_io_abort_and_busy(void)
{
    const float bad_frequencies[] = {1000.0F, 500.0F};
    sweep_point_t points[2];
    fake_port_t port = {0};
    sweep_service_t service;
    sweep_result_t result;
    sweep_config_t config = make_config();
    sweep_ops_t ops = make_ops(&port);
    sweep_request_t request = {bad_frequencies, 2U, points, 2U};

    assert(sweep_service_init(&service, &config, &ops) == SWEEP_STATUS_OK);
    assert(sweep_service_start(&service, &request, 0U)
           == SWEEP_STATUS_INVALID_ARGUMENT);
    request.point_count = 1U;
    port.fail_set = true;
    assert(sweep_service_start(&service, &request, 0U)
           == SWEEP_STATUS_IO_ERROR);
    assert(sweep_service_get_result(&service, &result) == SWEEP_STATUS_OK);
    assert(result.reason == SWEEP_REASON_IO_ERROR);

    port.fail_set = false;
    assert(sweep_service_start(&service, &request, 10U)
           == SWEEP_STATUS_IN_PROGRESS);
    assert(sweep_service_start(&service, &request, 11U)
           == SWEEP_STATUS_BUSY);
    assert(sweep_service_abort(&service) == SWEEP_STATUS_OK);
    assert(sweep_service_get_result(&service, &result) == SWEEP_STATUS_OK);
    assert(result.reason == SWEEP_REASON_ABORTED);
}

int main(void)
{
    test_happy_path_and_order();
    test_retry_then_success();
    test_point_and_total_timeout();
    test_invalid_io_abort_and_busy();
    (void)puts("sweep_service tests passed");
    return 0;
}
