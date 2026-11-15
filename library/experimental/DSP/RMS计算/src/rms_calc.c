#include "rms_calc.h"

#include <math.h>

static rms_status_t validate_input(const float *samples, size_t length)
{
    size_t i;

    if (samples == 0) {
        return RMS_NULL;
    }
    if (length == 0U) {
        return RMS_LENGTH_INVALID;
    }
    for (i = 0U; i < length; ++i) {
        if (!isfinite(samples[i])) {
            return RMS_NON_FINITE;
        }
    }
    return RMS_OK;
}

static rms_status_t scaled_rms(const float *samples,
                               size_t length,
                               float offset,
                               float *result)
{
    float scale = 0.0F;
    float sumsq = 1.0F;
    size_t i;

    for (i = 0U; i < length; ++i) {
        const float value = samples[i] - offset;
        const float magnitude = fabsf(value);

        if (!isfinite(value)) {
            return RMS_OUTPUT_INVALID;
        }
        if (magnitude != 0.0F) {
            if (scale < magnitude) {
                const float ratio = scale / magnitude;
                sumsq = 1.0F + sumsq * ratio * ratio;
                scale = magnitude;
            } else {
                const float ratio = magnitude / scale;
                sumsq += ratio * ratio;
            }
        }
    }

    if (scale == 0.0F) {
        *result = 0.0F;
    } else {
        const float value = scale * sqrtf(sumsq / (float)length);
        if (!isfinite(value)) {
            return RMS_OUTPUT_INVALID;
        }
        *result = value;
    }
    return RMS_OK;
}

rms_status_t rms_calc_mean(const float *samples, size_t length, float *mean)
{
    rms_status_t status;
    float value = 0.0F;
    size_t i;

    if (mean == 0) {
        return RMS_NULL;
    }
    status = validate_input(samples, length);
    if (status != RMS_OK) {
        return status;
    }

    for (i = 0U; i < length; ++i) {
        value += (samples[i] - value) / (float)(i + 1U);
        if (!isfinite(value)) {
            return RMS_OUTPUT_INVALID;
        }
    }

    *mean = value;
    return RMS_OK;
}

rms_status_t rms_calc_total(const float *samples, size_t length, float *rms)
{
    rms_status_t status;
    float value;

    if (rms == 0) {
        return RMS_NULL;
    }
    status = validate_input(samples, length);
    if (status != RMS_OK) {
        return status;
    }
    status = scaled_rms(samples, length, 0.0F, &value);
    if (status != RMS_OK) {
        return status;
    }

    *rms = value;
    return RMS_OK;
}

rms_status_t rms_calc_ac(const float *samples, size_t length, float *rms)
{
    rms_status_t status;
    float mean;
    float value;

    if (rms == 0) {
        return RMS_NULL;
    }
    status = rms_calc_mean(samples, length, &mean);
    if (status != RMS_OK) {
        return status;
    }
    status = scaled_rms(samples, length, mean, &value);
    if (status != RMS_OK) {
        return status;
    }

    *rms = value;
    return RMS_OK;
}
