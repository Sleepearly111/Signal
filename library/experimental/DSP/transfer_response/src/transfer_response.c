#include "transfer_response.h"

#include <float.h>
#include <math.h>
#include <string.h>

static bool finite_value(float value)
{
    return (value <= FLT_MAX) && (value >= -FLT_MAX);
}

static bool usable_measurement(
    const transfer_response_measurement_t *measurement,
    const transfer_response_config_t *config
)
{
    float gain;

    if (!measurement->valid ||
        measurement->input_saturated ||
        measurement->output_saturated ||
        !finite_value(measurement->input_value) ||
        !finite_value(measurement->output_value) ||
        (measurement->input_value < config->minimum_input_value) ||
        (measurement->output_value < 0.0F)) {
        return false;
    }
    gain = measurement->output_value / measurement->input_value;
    return finite_value(gain) &&
           (gain > 0.0F) &&
           (gain <= config->maximum_gain_linear);
}

transfer_response_status_t transfer_response_prepare(
    const transfer_response_measurement_t *measurements,
    size_t measurement_count,
    const transfer_response_config_t *config,
    transfer_response_point_t *output_points,
    size_t output_capacity,
    transfer_response_summary_t *summary
)
{
    size_t input_index;
    size_t output_index = 0U;

    if ((measurements == NULL) || (config == NULL) ||
        (output_points == NULL) || (summary == NULL) ||
        (measurement_count == 0U) ||
        (config->minimum_input_value <= 0.0F) ||
        !finite_value(config->minimum_input_value) ||
        (config->maximum_gain_linear <= 0.0F) ||
        !finite_value(config->maximum_gain_linear) ||
        (config->minimum_valid_points == 0U)) {
        return TRANSFER_RESPONSE_INVALID_ARGUMENT;
    }
    if (output_capacity < measurement_count) {
        return TRANSFER_RESPONSE_OUTPUT_TOO_SMALL;
    }

    (void)memset(summary, 0, sizeof(*summary));
    for (input_index = 0U; input_index < measurement_count; ++input_index) {
        const transfer_response_measurement_t *measurement =
            &measurements[input_index];
        float gain;

        if (!finite_value(measurement->frequency_hz) ||
            (measurement->frequency_hz <= 0.0F) ||
            ((input_index > 0U) &&
             (measurement->frequency_hz <=
              measurements[input_index - 1U].frequency_hz))) {
            return TRANSFER_RESPONSE_INVALID_DATA;
        }
        if (!usable_measurement(measurement, config)) {
            summary->rejected_count++;
            continue;
        }

        gain = measurement->output_value / measurement->input_value;
        output_points[output_index].frequency_hz =
            measurement->frequency_hz;
        output_points[output_index].gain_linear = gain;
        output_points[output_index].gain_db = 20.0F * log10f(gain);
        if (!finite_value(output_points[output_index].gain_db)) {
            return TRANSFER_RESPONSE_INVALID_DATA;
        }
        if (output_index == 0U) {
            summary->peak_gain_db = output_points[0].gain_db;
            summary->notch_gain_db = output_points[0].gain_db;
        } else {
            if (output_points[output_index].gain_db >
                summary->peak_gain_db) {
                summary->peak_gain_db =
                    output_points[output_index].gain_db;
                summary->peak_index = output_index;
            }
            if (output_points[output_index].gain_db <
                summary->notch_gain_db) {
                summary->notch_gain_db =
                    output_points[output_index].gain_db;
                summary->notch_index = output_index;
            }
        }
        output_index++;
    }

    summary->valid_count = output_index;
    if (output_index > 0U) {
        summary->dynamic_span_db =
            summary->peak_gain_db - summary->notch_gain_db;
    }
    if (output_index < config->minimum_valid_points) {
        return TRANSFER_RESPONSE_INSUFFICIENT_VALID_POINTS;
    }
    return TRANSFER_RESPONSE_OK;
}

transfer_response_status_t transfer_response_find_level_crossings(
    const transfer_response_point_t *points,
    size_t point_count,
    float level_below_peak_db,
    float *crossing_frequencies_hz,
    size_t crossing_capacity,
    size_t *crossing_count
)
{
    size_t index;
    float peak_db;
    float level_db;
    size_t found = 0U;

    if ((points == NULL) || (crossing_frequencies_hz == NULL) ||
        (crossing_count == NULL) || (point_count < 2U) ||
        (crossing_capacity == 0U) ||
        !finite_value(level_below_peak_db) ||
        (level_below_peak_db <= 0.0F)) {
        return TRANSFER_RESPONSE_INVALID_ARGUMENT;
    }

    peak_db = points[0].gain_db;
    for (index = 0U; index < point_count; ++index) {
        if (!finite_value(points[index].frequency_hz) ||
            !finite_value(points[index].gain_db) ||
            (points[index].frequency_hz <= 0.0F) ||
            ((index > 0U) &&
             (points[index].frequency_hz <=
              points[index - 1U].frequency_hz))) {
            return TRANSFER_RESPONSE_INVALID_DATA;
        }
        if (points[index].gain_db > peak_db) {
            peak_db = points[index].gain_db;
        }
    }
    level_db = peak_db - level_below_peak_db;

    for (index = 0U; index + 1U < point_count; ++index) {
        const float a = points[index].gain_db - level_db;
        const float b = points[index + 1U].gain_db - level_db;
        float fraction;
        float log_frequency;

        if ((a == 0.0F) && (b == 0.0F)) {
            continue;
        }
        if (!(((a <= 0.0F) && (b > 0.0F)) ||
              ((a >= 0.0F) && (b < 0.0F)))) {
            continue;
        }
        if (found >= crossing_capacity) {
            return TRANSFER_RESPONSE_OUTPUT_TOO_SMALL;
        }

        fraction = a / (a - b);
        log_frequency =
            log10f(points[index].frequency_hz) +
            fraction *
            (log10f(points[index + 1U].frequency_hz) -
             log10f(points[index].frequency_hz));
        crossing_frequencies_hz[found] = powf(10.0F, log_frequency);
        if (!finite_value(crossing_frequencies_hz[found])) {
            return TRANSFER_RESPONSE_INVALID_DATA;
        }
        found++;
    }

    *crossing_count = found;
    return TRANSFER_RESPONSE_OK;
}
