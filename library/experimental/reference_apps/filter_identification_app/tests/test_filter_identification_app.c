#include "filter_identification_app.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef enum {
    FAKE_RESPONSE_LOW_PASS = 0,
    FAKE_RESPONSE_FLAT,
    FAKE_RESPONSE_INVALID
} fake_response_t;

typedef struct {
    float frequency_hz;
    unsigned stop_calls;
    fake_response_t response;
} fake_port_t;

static sweep_port_status_t fake_set(void *context, float frequency_hz)
{
    fake_port_t *port = (fake_port_t *)context;
    port->frequency_hz = frequency_hz;
    return SWEEP_PORT_OK;
}

static sweep_port_status_t fake_start(void *context)
{
    (void)context;
    return SWEEP_PORT_OK;
}

static float low_pass_gain(float frequency_hz)
{
    if (frequency_hz <= 1000.0F) {
        return 1.0F;
    }
    if (frequency_hz <= 2000.0F) {
        return 0.8F;
    }
    if (frequency_hz <= 4000.0F) {
        return 0.5F;
    }
    if (frequency_hz <= 8000.0F) {
        return 0.2F;
    }
    return 0.05F;
}

static sweep_port_status_t fake_poll(
    void *context,
    bool *done,
    sweep_point_t *point
)
{
    fake_port_t *port = (fake_port_t *)context;
    *done = true;
    point->input_value = 1.0F;
    point->output_value =
        (port->response == FAKE_RESPONSE_LOW_PASS)
            ? low_pass_gain(port->frequency_hz)
            : 1.0F;
    point->valid = port->response != FAKE_RESPONSE_INVALID;
    point->saturated = false;
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

static filter_id_app_config_t make_config(void)
{
    filter_id_app_config_t config;
    config.sweep.settle_ms = 1U;
    config.sweep.point_timeout_ms = 10U;
    config.sweep.total_timeout_ms = 100U;
    config.sweep.max_retries_per_point = 0U;
    config.response.minimum_input_value = 0.1F;
    config.response.maximum_gain_linear = 2.0F;
    config.response.minimum_valid_points = 5U;
    config.classifier.passband_min_db = -3.0F;
    config.classifier.stopband_max_db = -10.0F;
    config.classifier.min_contrast_db = 7.0F;
    config.classifier.edge_window_points = 1U;
    config.crossing_level_below_peak_db = 3.0F;
    return config;
}

static filter_id_app_result_t run_case(
    fake_port_t *port,
    filter_id_app_state_t *final_state
)
{
    static const float frequencies[] = {
        500.0F, 1000.0F, 2000.0F, 4000.0F,
        8000.0F, 16000.0F, 20000.0F
    };
    filter_id_app_t app;
    filter_id_app_result_t result;
    filter_id_app_config_t config = make_config();
    sweep_ops_t ops = make_ops(port);
    filter_id_app_request_t request = {frequencies, 7U};
    uint32_t now_ms = 0U;

    assert(filter_id_app_init(&app, &config, &ops)
           == FILTER_ID_APP_STATUS_OK);
    assert(filter_id_app_start(&app, &request, now_ms)
           == FILTER_ID_APP_STATUS_IN_PROGRESS);
    while (!filter_id_app_is_done(&app)) {
        now_ms++;
        assert(now_ms < 100U);
        (void)filter_id_app_service(&app, now_ms);
    }
    assert(filter_id_app_get_result(&app, &result)
           == FILTER_ID_APP_STATUS_OK);
    *final_state = filter_id_app_get_state(&app);
    return result;
}

static void test_low_pass_success(void)
{
    fake_port_t port = {0};
    filter_id_app_state_t state;
    filter_id_app_result_t result;

    port.response = FAKE_RESPONSE_LOW_PASS;
    result = run_case(&port, &state);
    assert(state == FILTER_ID_APP_STATE_DONE);
    assert(result.reason == FILTER_ID_APP_REASON_SUCCESS);
    assert(result.type == FILTER_TYPE_LOW_PASS);
    assert(result.measured_points == 7U);
    assert(result.valid_points == 7U);
    assert(result.crossing_count == 1U);
    assert(port.stop_calls == 1U);
}

static void test_ambiguous_is_safe_unknown(void)
{
    fake_port_t port = {0};
    filter_id_app_state_t state;
    filter_id_app_result_t result;

    port.response = FAKE_RESPONSE_FLAT;
    result = run_case(&port, &state);
    assert(state == FILTER_ID_APP_STATE_DONE);
    assert(result.reason == FILTER_ID_APP_REASON_AMBIGUOUS);
    assert(result.type == FILTER_TYPE_UNKNOWN);
}

static void test_invalid_measurement_fault(void)
{
    fake_port_t port = {0};
    filter_id_app_state_t state;
    filter_id_app_result_t result;

    port.response = FAKE_RESPONSE_INVALID;
    result = run_case(&port, &state);
    assert(state == FILTER_ID_APP_STATE_FAULT);
    assert(result.reason == FILTER_ID_APP_REASON_SWEEP_FAILED);
    assert(result.sweep_reason ==
           SWEEP_REASON_INVALID_MEASUREMENT);
}

static void test_abort(void)
{
    static const float frequencies[] = {
        500.0F, 1000.0F, 2000.0F, 4000.0F, 8000.0F
    };
    fake_port_t port = {0};
    filter_id_app_t app;
    filter_id_app_result_t result;
    filter_id_app_config_t config = make_config();
    sweep_ops_t ops = make_ops(&port);
    filter_id_app_request_t request = {frequencies, 5U};

    assert(filter_id_app_init(&app, &config, &ops)
           == FILTER_ID_APP_STATUS_OK);
    assert(filter_id_app_start(&app, &request, 0U)
           == FILTER_ID_APP_STATUS_IN_PROGRESS);
    assert(filter_id_app_abort(&app) == FILTER_ID_APP_STATUS_OK);
    assert(filter_id_app_get_result(&app, &result)
           == FILTER_ID_APP_STATUS_OK);
    assert(result.reason == FILTER_ID_APP_REASON_ABORTED);
    assert(port.stop_calls == 1U);
}

int main(void)
{
    test_low_pass_success();
    test_ambiguous_is_safe_unknown();
    test_invalid_measurement_fault();
    test_abort();
    (void)puts("filter_identification_app tests passed");
    return 0;
}
