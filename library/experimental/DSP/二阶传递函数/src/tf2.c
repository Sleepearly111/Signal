#include "tf2.h"

#include <math.h>

#define TF2_PI 3.14159265358979323846264338327950288

static int tf2_type_is_valid(tf2_type_t type)
{
    return type == TF2_LOWPASS || type == TF2_HIGHPASS ||
           type == TF2_BANDPASS || type == TF2_BANDSTOP;
}

static double tf2_normalize_phase(double phase_rad)
{
    while (phase_rad > TF2_PI) {
        phase_rad -= 2.0 * TF2_PI;
    }
    while (phase_rad <= -TF2_PI) {
        phase_rad += 2.0 * TF2_PI;
    }
    return phase_rad;
}

tf2_status_t tf2_init(tf2_model_t *model,
                      tf2_type_t type,
                      double gain,
                      double f0_hz,
                      double q)
{
    if (model == 0) {
        return TF2_STATUS_NULL_POINTER;
    }
    if (!tf2_type_is_valid(type)) {
        return TF2_STATUS_INVALID_TYPE;
    }
    if (!isfinite(gain) || !isfinite(f0_hz) || !isfinite(q) ||
        gain < 0.0 || f0_hz <= 0.0 || q <= 0.0) {
        return TF2_STATUS_INVALID_PARAMETER;
    }

    model->type = type;
    model->gain = gain;
    model->f0_hz = f0_hz;
    model->q = q;
    return TF2_STATUS_OK;
}

tf2_status_t tf2_calc_response(const tf2_model_t *model,
                               double frequency_hz,
                               tf2_response_t *response)
{
    double x;
    double x2;
    double denominator_real;
    double denominator_imag;
    double numerator_real = 0.0;
    double numerator_imag = 0.0;
    double numerator_magnitude;
    double denominator_magnitude;
    double magnitude;
    double phase;

    if (model == 0 || response == 0) {
        return TF2_STATUS_NULL_POINTER;
    }
    if (!tf2_type_is_valid(model->type)) {
        return TF2_STATUS_INVALID_TYPE;
    }
    if (!isfinite(model->gain) || !isfinite(model->f0_hz) ||
        !isfinite(model->q) || model->gain < 0.0 ||
        model->f0_hz <= 0.0 || model->q <= 0.0 ||
        !isfinite(frequency_hz) || frequency_hz < 0.0) {
        return TF2_STATUS_INVALID_PARAMETER;
    }

    x = frequency_hz / model->f0_hz;
    x2 = x * x;
    denominator_real = 1.0 - x2;
    denominator_imag = x / model->q;

    switch (model->type) {
    case TF2_LOWPASS:
        numerator_real = 1.0;
        break;
    case TF2_HIGHPASS:
        numerator_real = -x2;
        break;
    case TF2_BANDPASS:
        numerator_imag = x / model->q;
        break;
    case TF2_BANDSTOP:
        numerator_real = 1.0 - x2;
        break;
    default:
        return TF2_STATUS_INVALID_TYPE;
    }

    numerator_magnitude = hypot(numerator_real, numerator_imag);
    denominator_magnitude = hypot(denominator_real, denominator_imag);
    if (!isfinite(denominator_magnitude) || denominator_magnitude == 0.0) {
        return TF2_STATUS_INVALID_PARAMETER;
    }

    magnitude = model->gain * numerator_magnitude / denominator_magnitude;
    if (magnitude == 0.0) {
        phase = 0.0;
    } else {
        phase = atan2(numerator_imag, numerator_real) -
                atan2(denominator_imag, denominator_real);
        phase = tf2_normalize_phase(phase);
    }

    if (!isfinite(magnitude) || !isfinite(phase)) {
        return TF2_STATUS_INVALID_PARAMETER;
    }

    response->magnitude = magnitude;
    response->phase_rad = phase;
    return TF2_STATUS_OK;
}
