#include "regulated_signal_output_app.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    float plant_gain;
    float control;
    float frequency_hz;
    regulated_output_waveform_t waveform;
    unsigned configure_calls;
    unsigned set_calls;
    unsigned start_calls;
    unsigned poll_calls;
    unsigned stop_calls;
    bool invalid;
    bool saturated;
    bool never_done;
    bool fail_set;
} fake_port_t;

static regulated_output_port_status_t fake_configure(
    void *context,
    float frequency_hz,
    regulated_output_waveform_t waveform
)
{
    fake_port_t *port = (fake_port_t *)context;
    port->frequency_hz = frequency_hz;
    port->waveform = waveform;
    port->configure_calls++;
    return REGULATED_OUTPUT_PORT_OK;
}

static regulated_output_port_status_t fake_set(
    void *context,
    float control
)
{
    fake_port_t *port = (fake_port_t *)context;
    port->set_calls++;
    if (port->fail_set) {
        return REGULATED_OUTPUT_PORT_ERROR;
    }
    port->control = control;
    return REGULATED_OUTPUT_PORT_OK;
}

static regulated_output_port_status_t fake_start(void *context)
{
    fake_port_t *port = (fake_port_t *)context;
    port->start_calls++;
    return REGULATED_OUTPUT_PORT_OK;
}

static regulated_output_port_status_t fake_poll(
    void *context,
    bool *done,
    float *measured_vpp,
    bool *valid,
    bool *saturated
)
{
    fake_port_t *port = (fake_port_t *)context;
    port->poll_calls++;
    *done = !port->never_done;
    *measured_vpp = port->plant_gain * port->control;
    *valid = !port->invalid;
    *saturated = port->saturated;
    return REGULATED_OUTPUT_PORT_OK;
}

static void fake_safe_stop(void *context)
{
    fake_port_t *port = (fake_port_t *)context;
    port->stop_calls++;
    port->control = 0.0F;
}

static regulated_output_config_t make_config(void)
{
    regulated_output_config_t config;
    config.settle_ms = 1U;
    config.total_timeout_ms = 100U;
    config.max_iterations = 4U;
    config.minimum_control = 0.1F;
    config.maximum_control = 3.0F;
    config.minimum_valid_vpp = 0.05F;
    config.absolute_tolerance_vpp = 0.01F;
    config.relative_tolerance = 0.02F;
    config.minimum_correction_ratio = 0.7F;
    config.maximum_correction_ratio = 1.3F;
    return config;
}

static regulated_output_ops_t make_ops(fake_port_t *port)
{
    regulated_output_ops_t ops;
    (void)memset(&ops, 0, sizeof(ops));
    ops.configure_signal = fake_configure;
    ops.set_control = fake_set;
    ops.start_measurement = fake_start;
    ops.poll_measurement = fake_poll;
    ops.safe_stop = fake_safe_stop;
    ops.user_context = port;
    return ops;
}

static regulated_output_result_t run_to_terminal(
    regulated_output_app_t *app,
    uint32_t start_ms
)
{
    regulated_output_result_t result;
    uint32_t now_ms = start_ms;
    unsigned guard = 0U;

    while (!regulated_output_app_is_done(app)) {
        now_ms++;
        (void)regulated_output_app_service(app, now_ms);
        guard++;
        assert(guard < 200U);
    }
    assert(regulated_output_app_get_result(app, &result) ==
           REGULATED_OUTPUT_STATUS_OK);
    return result;
}

static void test_converges_and_holds_output(void)
{
    fake_port_t port = {0};
    regulated_output_app_t app;
    regulated_output_result_t result;
    regulated_output_config_t config = make_config();
    regulated_output_ops_t ops = make_ops(&port);
    regulated_output_request_t request = {
        10000.0F,
        2.0F,
        0.5F,
        REGULATED_OUTPUT_WAVEFORM_SINE
    };

    port.plant_gain = 2.0F;
    assert(regulated_output_app_init(&app, &config, &ops) ==
           REGULATED_OUTPUT_STATUS_OK);
    assert(regulated_output_app_start(&app, &request, 0U) ==
           REGULATED_OUTPUT_STATUS_IN_PROGRESS);
    result = run_to_terminal(&app, 0U);
    assert(result.reason == REGULATED_OUTPUT_REASON_CONVERGED);
    assert(result.iterations > 0U);
    assert(result.final_vpp > 1.95F);
    assert(result.final_vpp < 2.05F);
    assert(result.final_control == port.control);
    assert(port.frequency_hz == 10000.0F);
    assert(port.waveform == REGULATED_OUTPUT_WAVEFORM_SINE);
    assert(port.stop_calls == 0U);
}

static void test_unreachable_target_stops_safely(void)
{
    fake_port_t port = {0};
    regulated_output_app_t app;
    regulated_output_result_t result;
    regulated_output_config_t config = make_config();
    regulated_output_ops_t ops = make_ops(&port);
    regulated_output_request_t request = {
        1000.0F,
        4.0F,
        1.0F,
        REGULATED_OUTPUT_WAVEFORM_SQUARE
    };

    port.plant_gain = 1.0F;
    config.maximum_control = 2.0F;
    config.maximum_correction_ratio = 4.0F;
    config.max_iterations = 2U;
    assert(regulated_output_app_init(&app, &config, &ops) ==
           REGULATED_OUTPUT_STATUS_OK);
    assert(regulated_output_app_start(&app, &request, 0U) ==
           REGULATED_OUTPUT_STATUS_IN_PROGRESS);
    result = run_to_terminal(&app, 0U);
    assert(result.reason ==
           REGULATED_OUTPUT_REASON_MAX_ITERATIONS);
    assert(result.final_control == 2.0F);
    assert(port.control == 0.0F);
    assert(port.stop_calls == 1U);
}

static void test_quality_faults(void)
{
    fake_port_t port = {0};
    regulated_output_app_t app;
    regulated_output_result_t result;
    regulated_output_config_t config = make_config();
    regulated_output_ops_t ops = make_ops(&port);
    regulated_output_request_t request = {
        5000.0F,
        1.0F,
        0.5F,
        REGULATED_OUTPUT_WAVEFORM_TRIANGLE
    };

    port.plant_gain = 1.0F;
    port.invalid = true;
    assert(regulated_output_app_init(&app, &config, &ops) ==
           REGULATED_OUTPUT_STATUS_OK);
    assert(regulated_output_app_start(&app, &request, 0U) ==
           REGULATED_OUTPUT_STATUS_IN_PROGRESS);
    result = run_to_terminal(&app, 0U);
    assert(result.reason ==
           REGULATED_OUTPUT_REASON_INVALID_MEASUREMENT);
    assert(port.stop_calls == 1U);

    port.invalid = false;
    port.saturated = true;
    assert(regulated_output_app_start(&app, &request, 20U) ==
           REGULATED_OUTPUT_STATUS_IN_PROGRESS);
    result = run_to_terminal(&app, 20U);
    assert(result.reason == REGULATED_OUTPUT_REASON_SATURATED);
    assert(port.stop_calls == 2U);
}

static void test_timeout_wrap_and_abort(void)
{
    fake_port_t port = {0};
    regulated_output_app_t app;
    regulated_output_result_t result;
    regulated_output_config_t config = make_config();
    regulated_output_ops_t ops = make_ops(&port);
    regulated_output_request_t request = {
        2000.0F,
        1.0F,
        0.5F,
        REGULATED_OUTPUT_WAVEFORM_SINE
    };
    const uint32_t start = UINT32_MAX - 3U;

    port.plant_gain = 1.0F;
    port.never_done = true;
    config.total_timeout_ms = 5U;
    assert(regulated_output_app_init(&app, &config, &ops) ==
           REGULATED_OUTPUT_STATUS_OK);
    assert(regulated_output_app_start(&app, &request, start) ==
           REGULATED_OUTPUT_STATUS_IN_PROGRESS);
    result = run_to_terminal(&app, start);
    assert(result.reason == REGULATED_OUTPUT_REASON_TIMEOUT);
    assert(port.stop_calls == 1U);

    port.never_done = false;
    assert(regulated_output_app_start(&app, &request, 20U) ==
           REGULATED_OUTPUT_STATUS_IN_PROGRESS);
    assert(regulated_output_app_abort(&app) ==
           REGULATED_OUTPUT_STATUS_OK);
    assert(regulated_output_app_get_result(&app, &result) ==
           REGULATED_OUTPUT_STATUS_OK);
    assert(result.reason == REGULATED_OUTPUT_REASON_ABORTED);
    assert(port.stop_calls == 2U);
}

static void test_invalid_config_and_io_failure(void)
{
    fake_port_t port = {0};
    regulated_output_app_t app;
    regulated_output_result_t result;
    regulated_output_config_t config = make_config();
    regulated_output_ops_t ops = make_ops(&port);
    regulated_output_request_t request = {
        1000.0F,
        1.0F,
        0.5F,
        REGULATED_OUTPUT_WAVEFORM_SINE
    };

    config.minimum_correction_ratio = 1.1F;
    assert(regulated_output_app_init(&app, &config, &ops) ==
           REGULATED_OUTPUT_STATUS_INVALID_ARGUMENT);

    config = make_config();
    port.plant_gain = 1.0F;
    port.fail_set = true;
    assert(regulated_output_app_init(&app, &config, &ops) ==
           REGULATED_OUTPUT_STATUS_OK);
    assert(regulated_output_app_start(&app, &request, 0U) ==
           REGULATED_OUTPUT_STATUS_IO_ERROR);
    assert(regulated_output_app_get_result(&app, &result) ==
           REGULATED_OUTPUT_STATUS_OK);
    assert(result.reason == REGULATED_OUTPUT_REASON_IO_ERROR);
    assert(port.stop_calls == 1U);
}

int main(void)
{
    test_converges_and_holds_output();
    test_unreachable_target_stops_safely();
    test_quality_faults();
    test_timeout_wrap_and_abort();
    test_invalid_config_and_io_failure();
    (void)puts("regulated_signal_output_app tests passed");
    return 0;
}
