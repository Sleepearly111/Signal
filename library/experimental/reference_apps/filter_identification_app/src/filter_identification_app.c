#include "filter_identification_app.h"

#include <float.h>
#include <string.h>

static bool finite_value(float value)
{
    return (value <= FLT_MAX) && (value >= -FLT_MAX);
}

static bool terminal_state(filter_id_app_state_t state)
{
    return (state == FILTER_ID_APP_STATE_DONE) ||
           (state == FILTER_ID_APP_STATE_FAULT) ||
           (state == FILTER_ID_APP_STATE_ABORTED);
}

static filter_id_app_status_t fail(
    filter_id_app_t *app,
    filter_id_app_reason_t reason,
    filter_id_app_status_t status
)
{
    app->state = FILTER_ID_APP_STATE_FAULT;
    app->result.reason = reason;
    app->result.type = FILTER_TYPE_UNKNOWN;
    return status;
}

static filter_id_app_status_t analyze(filter_id_app_t *app)
{
    transfer_response_summary_t summary;
    filter_classifier_result_t classifier_result;
    size_t index;
    size_t crossing_count = 0U;

    for (index = 0U; index < app->point_count; ++index) {
        app->measurements[index].frequency_hz =
            app->sweep_points[index].frequency_hz;
        app->measurements[index].input_value =
            app->sweep_points[index].input_value;
        app->measurements[index].output_value =
            app->sweep_points[index].output_value;
        app->measurements[index].valid =
            app->sweep_points[index].valid;
        app->measurements[index].input_saturated = false;
        app->measurements[index].output_saturated =
            app->sweep_points[index].saturated;
    }

    app->result.transfer_status = transfer_response_prepare(
        app->measurements,
        app->point_count,
        &app->config.response,
        app->response_points,
        FILTER_ID_APP_MAX_POINTS,
        &summary
    );
    app->result.valid_points = summary.valid_count;
    app->result.rejected_points = summary.rejected_count;
    app->result.dynamic_span_db = summary.dynamic_span_db;
    if (app->result.transfer_status != TRANSFER_RESPONSE_OK) {
        return fail(
            app,
            FILTER_ID_APP_REASON_TRANSFER_FAILED,
            FILTER_ID_APP_STATUS_OK
        );
    }

    app->result.peak_frequency_hz =
        app->response_points[summary.peak_index].frequency_hz;
    app->result.notch_frequency_hz =
        app->response_points[summary.notch_index].frequency_hz;
    for (index = 0U; index < summary.valid_count; ++index) {
        app->classifier_frequencies_hz[index] =
            app->response_points[index].frequency_hz;
        app->classifier_gain_db[index] =
            app->response_points[index].gain_db;
    }

    app->result.classifier_status = filter_classifier_classify(
        app->classifier_frequencies_hz,
        app->classifier_gain_db,
        summary.valid_count,
        &app->config.classifier,
        &classifier_result
    );
    app->result.type = classifier_result.type;
    app->result.confidence_margin_db =
        classifier_result.confidence_margin_db;
    if (app->result.classifier_status ==
        FILTER_CLASSIFIER_AMBIGUOUS) {
        app->result.reason = FILTER_ID_APP_REASON_AMBIGUOUS;
        app->result.type = FILTER_TYPE_UNKNOWN;
        app->state = FILTER_ID_APP_STATE_DONE;
        return FILTER_ID_APP_STATUS_OK;
    }
    if (app->result.classifier_status != FILTER_CLASSIFIER_OK) {
        return fail(
            app,
            FILTER_ID_APP_REASON_CLASSIFIER_FAILED,
            FILTER_ID_APP_STATUS_OK
        );
    }

    if (transfer_response_find_level_crossings(
            app->response_points,
            summary.valid_count,
            app->config.crossing_level_below_peak_db,
            app->result.crossing_frequencies_hz,
            FILTER_ID_APP_MAX_CROSSINGS,
            &crossing_count
        ) == TRANSFER_RESPONSE_OK) {
        app->result.crossing_count = crossing_count;
    }
    app->result.reason = FILTER_ID_APP_REASON_SUCCESS;
    app->state = FILTER_ID_APP_STATE_DONE;
    return FILTER_ID_APP_STATUS_OK;
}

filter_id_app_status_t filter_id_app_init(
    filter_id_app_t *app,
    const filter_id_app_config_t *config,
    const sweep_ops_t *sweep_ops
)
{
    if ((app == NULL) || (config == NULL) || (sweep_ops == NULL) ||
        !finite_value(config->crossing_level_below_peak_db) ||
        (config->crossing_level_below_peak_db <= 0.0F) ||
        !finite_value(config->response.minimum_input_value) ||
        (config->response.minimum_input_value <= 0.0F) ||
        !finite_value(config->response.maximum_gain_linear) ||
        (config->response.maximum_gain_linear <= 0.0F) ||
        (config->response.minimum_valid_points < 5U) ||
        (config->response.minimum_valid_points >
         FILTER_ID_APP_MAX_POINTS) ||
        !finite_value(config->classifier.passband_min_db) ||
        !finite_value(config->classifier.stopband_max_db) ||
        !finite_value(config->classifier.min_contrast_db) ||
        (config->classifier.passband_min_db <=
         config->classifier.stopband_max_db) ||
        (config->classifier.min_contrast_db <= 0.0F) ||
        (config->classifier.edge_window_points == 0U) ||
        ((2U * config->classifier.edge_window_points) >=
         config->response.minimum_valid_points)) {
        return FILTER_ID_APP_STATUS_INVALID_ARGUMENT;
    }

    (void)memset(app, 0, sizeof(*app));
    if (sweep_service_init(
            &app->sweep,
            &config->sweep,
            sweep_ops
        ) != SWEEP_STATUS_OK) {
        return FILTER_ID_APP_STATUS_INVALID_ARGUMENT;
    }
    app->config = *config;
    app->sweep_ops = *sweep_ops;
    app->state = FILTER_ID_APP_STATE_IDLE;
    app->initialized = true;
    return FILTER_ID_APP_STATUS_OK;
}

filter_id_app_status_t filter_id_app_start(
    filter_id_app_t *app,
    const filter_id_app_request_t *request,
    uint32_t now_ms
)
{
    sweep_request_t sweep_request;
    sweep_status_t status;
    size_t index;

    if ((app == NULL) || !app->initialized) {
        return FILTER_ID_APP_STATUS_NOT_INITIALIZED;
    }
    if ((app->state == FILTER_ID_APP_STATE_SWEEPING) ||
        (app->state == FILTER_ID_APP_STATE_ANALYZING)) {
        return FILTER_ID_APP_STATUS_BUSY;
    }
    if ((request == NULL) || (request->frequencies_hz == NULL) ||
        (request->point_count < 5U) ||
        (request->point_count > FILTER_ID_APP_MAX_POINTS)) {
        return FILTER_ID_APP_STATUS_INVALID_ARGUMENT;
    }

    (void)memset(&app->result, 0, sizeof(app->result));
    app->result.type = FILTER_TYPE_UNKNOWN;
    app->result.requested_points = request->point_count;
    app->point_count = request->point_count;
    for (index = 0U; index < request->point_count; ++index) {
        app->frequencies_hz[index] = request->frequencies_hz[index];
    }
    sweep_request.frequencies_hz = app->frequencies_hz;
    sweep_request.point_count = app->point_count;
    sweep_request.output_points = app->sweep_points;
    sweep_request.output_capacity = FILTER_ID_APP_MAX_POINTS;
    status = sweep_service_start(&app->sweep, &sweep_request, now_ms);
    if (status == SWEEP_STATUS_IO_ERROR) {
        app->result.sweep_reason = SWEEP_REASON_IO_ERROR;
        return fail(
            app,
            FILTER_ID_APP_REASON_IO_ERROR,
            FILTER_ID_APP_STATUS_IO_ERROR
        );
    }
    if (status != SWEEP_STATUS_IN_PROGRESS) {
        return FILTER_ID_APP_STATUS_INVALID_ARGUMENT;
    }
    app->state = FILTER_ID_APP_STATE_SWEEPING;
    return FILTER_ID_APP_STATUS_IN_PROGRESS;
}

filter_id_app_status_t filter_id_app_service(
    filter_id_app_t *app,
    uint32_t now_ms
)
{
    sweep_status_t status;
    sweep_result_t sweep_result;

    if ((app == NULL) || !app->initialized) {
        return FILTER_ID_APP_STATUS_NOT_INITIALIZED;
    }
    if (app->state == FILTER_ID_APP_STATE_ANALYZING) {
        return analyze(app);
    }
    if (app->state != FILTER_ID_APP_STATE_SWEEPING) {
        return terminal_state(app->state)
            ? FILTER_ID_APP_STATUS_OK
            : FILTER_ID_APP_STATUS_NOT_READY;
    }

    status = sweep_service_service(&app->sweep, now_ms);
    if (status == SWEEP_STATUS_IO_ERROR) {
        app->result.sweep_reason = SWEEP_REASON_IO_ERROR;
        return fail(
            app,
            FILTER_ID_APP_REASON_IO_ERROR,
            FILTER_ID_APP_STATUS_IO_ERROR
        );
    }
    if (!sweep_service_is_done(&app->sweep)) {
        return FILTER_ID_APP_STATUS_IN_PROGRESS;
    }
    if (sweep_service_get_result(&app->sweep, &sweep_result)
        != SWEEP_STATUS_OK) {
        return fail(
            app,
            FILTER_ID_APP_REASON_SWEEP_FAILED,
            FILTER_ID_APP_STATUS_OK
        );
    }
    app->result.sweep_reason = sweep_result.reason;
    app->result.measured_points = sweep_result.completed_points;
    if (sweep_result.reason != SWEEP_REASON_COMPLETED) {
        return fail(
            app,
            FILTER_ID_APP_REASON_SWEEP_FAILED,
            FILTER_ID_APP_STATUS_OK
        );
    }
    app->state = FILTER_ID_APP_STATE_ANALYZING;
    return FILTER_ID_APP_STATUS_IN_PROGRESS;
}

bool filter_id_app_is_done(const filter_id_app_t *app)
{
    return (app != NULL) && terminal_state(app->state);
}

filter_id_app_status_t filter_id_app_get_result(
    const filter_id_app_t *app,
    filter_id_app_result_t *result
)
{
    if ((app == NULL) || !app->initialized) {
        return FILTER_ID_APP_STATUS_NOT_INITIALIZED;
    }
    if (result == NULL) {
        return FILTER_ID_APP_STATUS_INVALID_ARGUMENT;
    }
    if (!terminal_state(app->state)) {
        return FILTER_ID_APP_STATUS_NOT_READY;
    }
    *result = app->result;
    return FILTER_ID_APP_STATUS_OK;
}

filter_id_app_status_t filter_id_app_abort(filter_id_app_t *app)
{
    if ((app == NULL) || !app->initialized) {
        return FILTER_ID_APP_STATUS_NOT_INITIALIZED;
    }
    if (app->state == FILTER_ID_APP_STATE_SWEEPING) {
        if (sweep_service_abort(&app->sweep) != SWEEP_STATUS_OK) {
            return FILTER_ID_APP_STATUS_IO_ERROR;
        }
    } else if (app->state != FILTER_ID_APP_STATE_ANALYZING) {
        return FILTER_ID_APP_STATUS_NOT_READY;
    }
    app->state = FILTER_ID_APP_STATE_ABORTED;
    app->result.reason = FILTER_ID_APP_REASON_ABORTED;
    app->result.type = FILTER_TYPE_UNKNOWN;
    return FILTER_ID_APP_STATUS_OK;
}

filter_id_app_state_t filter_id_app_get_state(
    const filter_id_app_t *app
)
{
    return (app != NULL)
        ? app->state
        : FILTER_ID_APP_STATE_UNINITIALIZED;
}

void filter_id_app_deinit(filter_id_app_t *app)
{
    if (app == NULL) {
        return;
    }
    sweep_service_deinit(&app->sweep);
    (void)memset(app, 0, sizeof(*app));
}
