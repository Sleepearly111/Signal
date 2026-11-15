#include "filter_classifier.h"

#include <float.h>
#include <stdbool.h>
#include <string.h>

static bool finite_value(float value)
{
    return (value <= FLT_MAX) && (value >= -FLT_MAX);
}

static float minimum(float a, float b)
{
    return (a < b) ? a : b;
}

filter_classifier_status_t filter_classifier_classify(
    const float *frequencies_hz,
    const float *gain_db,
    size_t count,
    const filter_classifier_config_t *config,
    filter_classifier_result_t *result
)
{
    size_t index;
    size_t edge;
    float maximum_gain;
    float minimum_gain;
    float low_sum = 0.0F;
    float high_sum = 0.0F;
    bool low_pass;
    bool high_pass;
    bool band_pass;
    bool band_stop;
    float margin;

    if ((frequencies_hz == NULL) || (gain_db == NULL) ||
        (config == NULL) || (result == NULL) || (count < 5U) ||
        (config->edge_window_points == 0U) ||
        ((2U * config->edge_window_points) >= count) ||
        (config->passband_min_db <= config->stopband_max_db) ||
        (config->min_contrast_db <= 0.0F)) {
        return FILTER_CLASSIFIER_INVALID_ARGUMENT;
    }

    (void)memset(result, 0, sizeof(*result));
    result->type = FILTER_TYPE_UNKNOWN;
    maximum_gain = gain_db[0];
    minimum_gain = gain_db[0];
    for (index = 0U; index < count; ++index) {
        if (!finite_value(frequencies_hz[index]) ||
            !finite_value(gain_db[index]) ||
            (frequencies_hz[index] <= 0.0F) ||
            ((index > 0U) &&
             (frequencies_hz[index] <= frequencies_hz[index - 1U]))) {
            return FILTER_CLASSIFIER_INVALID_DATA;
        }
        if (gain_db[index] > maximum_gain) {
            maximum_gain = gain_db[index];
            result->peak_index = index;
        }
        if (gain_db[index] < minimum_gain) {
            minimum_gain = gain_db[index];
            result->notch_index = index;
        }
    }

    edge = config->edge_window_points;
    for (index = 0U; index < edge; ++index) {
        low_sum += gain_db[index];
        high_sum += gain_db[count - edge + index];
    }
    result->low_edge_db = low_sum / (float)edge;
    result->high_edge_db = high_sum / (float)edge;
    result->peak_db = maximum_gain;
    result->notch_db = minimum_gain;

    low_pass =
        (result->low_edge_db >= config->passband_min_db) &&
        (result->high_edge_db <= config->stopband_max_db) &&
        ((result->low_edge_db - result->high_edge_db) >=
         config->min_contrast_db);
    high_pass =
        (result->high_edge_db >= config->passband_min_db) &&
        (result->low_edge_db <= config->stopband_max_db) &&
        ((result->high_edge_db - result->low_edge_db) >=
         config->min_contrast_db);
    band_pass =
        (result->low_edge_db <= config->stopband_max_db) &&
        (result->high_edge_db <= config->stopband_max_db) &&
        (maximum_gain >= config->passband_min_db) &&
        ((maximum_gain - result->low_edge_db) >=
         config->min_contrast_db) &&
        ((maximum_gain - result->high_edge_db) >=
         config->min_contrast_db) &&
        (result->peak_index >= edge) &&
        (result->peak_index < (count - edge));
    band_stop =
        (result->low_edge_db >= config->passband_min_db) &&
        (result->high_edge_db >= config->passband_min_db) &&
        (minimum_gain <= config->stopband_max_db) &&
        ((result->low_edge_db - minimum_gain) >=
         config->min_contrast_db) &&
        ((result->high_edge_db - minimum_gain) >=
         config->min_contrast_db) &&
        (result->notch_index >= edge) &&
        (result->notch_index < (count - edge));

    if (((unsigned)low_pass + (unsigned)high_pass +
         (unsigned)band_pass + (unsigned)band_stop) != 1U) {
        return FILTER_CLASSIFIER_AMBIGUOUS;
    }

    if (low_pass) {
        result->type = FILTER_TYPE_LOW_PASS;
        margin = minimum(
            result->low_edge_db - config->passband_min_db,
            config->stopband_max_db - result->high_edge_db
        );
    } else if (high_pass) {
        result->type = FILTER_TYPE_HIGH_PASS;
        margin = minimum(
            result->high_edge_db - config->passband_min_db,
            config->stopband_max_db - result->low_edge_db
        );
    } else if (band_pass) {
        result->type = FILTER_TYPE_BAND_PASS;
        margin = minimum(
            maximum_gain - config->passband_min_db,
            minimum(
                config->stopband_max_db - result->low_edge_db,
                config->stopband_max_db - result->high_edge_db
            )
        );
    } else {
        result->type = FILTER_TYPE_BAND_STOP;
        margin = minimum(
            minimum(
                result->low_edge_db - config->passband_min_db,
                result->high_edge_db - config->passband_min_db
            ),
            config->stopband_max_db - minimum_gain
        );
    }
    result->confidence_margin_db = margin;
    return FILTER_CLASSIFIER_OK;
}
