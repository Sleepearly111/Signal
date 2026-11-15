#include "filter_classifier.h"
#include "sweep_service.h"
#include "transfer_response.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    float current_frequency_hz;
    unsigned stop_calls;
} fake_measurement_port_t;

static sweep_port_status_t fake_set_frequency(
    void *context,
    float frequency_hz
)
{
    fake_measurement_port_t *port = (fake_measurement_port_t *)context;
    port->current_frequency_hz = frequency_hz;
    return SWEEP_PORT_OK;
}

static sweep_port_status_t fake_start_measurement(void *context)
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

static sweep_port_status_t fake_poll_measurement(
    void *context,
    bool *done,
    sweep_point_t *point
)
{
    const fake_measurement_port_t *port =
        (const fake_measurement_port_t *)context;
    *done = true;
    point->input_value = 1.0F;
    point->output_value = low_pass_gain(port->current_frequency_hz);
    point->valid = true;
    point->saturated = false;
    return SWEEP_PORT_OK;
}

static void fake_safe_stop(void *context)
{
    fake_measurement_port_t *port = (fake_measurement_port_t *)context;
    port->stop_calls++;
}

int main(void)
{
    static const float frequencies_hz[] = {
        500.0F, 1000.0F, 2000.0F, 4000.0F,
        8000.0F, 16000.0F, 20000.0F
    };
    sweep_point_t sweep_points[7];
    transfer_response_measurement_t measurements[7];
    transfer_response_point_t response_points[7];
    float classifier_frequencies[7];
    float classifier_gain_db[7];
    fake_measurement_port_t port = {0};
    sweep_service_t sweep;
    sweep_result_t sweep_result;
    transfer_response_summary_t response_summary;
    filter_classifier_result_t classifier_result;
    sweep_config_t sweep_config = {1U, 10U, 100U, 0U};
    sweep_ops_t sweep_ops;
    sweep_request_t sweep_request = {
        frequencies_hz, 7U, sweep_points, 7U
    };
    transfer_response_config_t response_config = {0.1F, 2.0F, 7U};
    filter_classifier_config_t classifier_config = {
        -3.0F, -10.0F, 7.0F, 1U
    };
    size_t index;
    uint32_t now_ms = 0U;

    (void)memset(&sweep_ops, 0, sizeof(sweep_ops));
    sweep_ops.set_frequency = fake_set_frequency;
    sweep_ops.start_measurement = fake_start_measurement;
    sweep_ops.poll_measurement = fake_poll_measurement;
    sweep_ops.safe_stop = fake_safe_stop;
    sweep_ops.user_context = &port;

    assert(sweep_service_init(&sweep, &sweep_config, &sweep_ops)
           == SWEEP_STATUS_OK);
    assert(sweep_service_start(&sweep, &sweep_request, now_ms)
           == SWEEP_STATUS_IN_PROGRESS);
    while (!sweep_service_is_done(&sweep)) {
        now_ms++;
        assert(now_ms < 100U);
        assert(sweep_service_service(&sweep, now_ms)
               != SWEEP_STATUS_IO_ERROR);
    }
    assert(sweep_service_get_result(&sweep, &sweep_result)
           == SWEEP_STATUS_OK);
    assert(sweep_result.reason == SWEEP_REASON_COMPLETED);
    assert(sweep_result.completed_points == 7U);
    assert(port.stop_calls == 1U);

    for (index = 0U; index < 7U; ++index) {
        measurements[index].frequency_hz =
            sweep_points[index].frequency_hz;
        measurements[index].input_value =
            sweep_points[index].input_value;
        measurements[index].output_value =
            sweep_points[index].output_value;
        measurements[index].valid = sweep_points[index].valid;
        measurements[index].input_saturated = false;
        measurements[index].output_saturated =
            sweep_points[index].saturated;
    }
    assert(transfer_response_prepare(
               measurements,
               7U,
               &response_config,
               response_points,
               7U,
               &response_summary
           ) == TRANSFER_RESPONSE_OK);
    assert(response_summary.valid_count == 7U);
    assert(response_summary.rejected_count == 0U);

    for (index = 0U; index < 7U; ++index) {
        classifier_frequencies[index] =
            response_points[index].frequency_hz;
        classifier_gain_db[index] = response_points[index].gain_db;
    }
    assert(filter_classifier_classify(
               classifier_frequencies,
               classifier_gain_db,
               7U,
               &classifier_config,
               &classifier_result
           ) == FILTER_CLASSIFIER_OK);
    assert(classifier_result.type == FILTER_TYPE_LOW_PASS);

    (void)puts("filter identification pipeline tests passed");
    return 0;
}
