#include "tf2.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

#define TEST_PI 3.14159265358979323846264338327950288

static int nearly_equal(double actual, double expected, double tolerance)
{
    return fabs(actual - expected) <= tolerance;
}

static tf2_response_t calculate(const tf2_model_t *model, double frequency_hz)
{
    tf2_response_t response = {-1.0, -99.0};
    assert(tf2_calc_response(model, frequency_hz, &response) == TF2_STATUS_OK);
    return response;
}

static void test_lowpass(void)
{
    tf2_model_t model;
    assert(tf2_init(&model, TF2_LOWPASS, 2.0, 1000.0, 0.7071067811865476) ==
           TF2_STATUS_OK);

    tf2_response_t low = calculate(&model, 10.0);
    tf2_response_t center = calculate(&model, 1000.0);
    tf2_response_t high = calculate(&model, 100000.0);

    assert(nearly_equal(low.magnitude, 2.0, 0.001));
    assert(nearly_equal(center.magnitude, 2.0 * model.q, 1.0e-12));
    assert(nearly_equal(center.phase_rad, -TEST_PI / 2.0, 1.0e-12));
    assert(low.magnitude > center.magnitude);
    assert(center.magnitude > high.magnitude);
    assert(high.magnitude < 0.001);
}

static void test_highpass(void)
{
    tf2_model_t model;
    assert(tf2_init(&model, TF2_HIGHPASS, 1.5, 2000.0, 0.8) ==
           TF2_STATUS_OK);

    tf2_response_t low = calculate(&model, 20.0);
    tf2_response_t center = calculate(&model, 2000.0);
    tf2_response_t high = calculate(&model, 200000.0);

    assert(low.magnitude < 0.001);
    assert(center.magnitude > low.magnitude);
    assert(nearly_equal(center.magnitude, 1.5 * model.q, 1.0e-12));
    assert(nearly_equal(center.phase_rad, TEST_PI / 2.0, 1.0e-12));
    assert(high.magnitude > center.magnitude);
    assert(nearly_equal(high.magnitude, 1.5, 0.001));
}

static void test_bandpass(void)
{
    tf2_model_t model;
    assert(tf2_init(&model, TF2_BANDPASS, 3.0, 5000.0, 1.2) ==
           TF2_STATUS_OK);

    tf2_response_t low = calculate(&model, 50.0);
    tf2_response_t center = calculate(&model, 5000.0);
    tf2_response_t high = calculate(&model, 500000.0);

    assert(center.magnitude > low.magnitude);
    assert(center.magnitude > high.magnitude);
    assert(nearly_equal(center.magnitude, 3.0, 1.0e-12));
    assert(nearly_equal(center.phase_rad, 0.0, 1.0e-12));
}

static void test_bandstop(void)
{
    tf2_model_t model;
    assert(tf2_init(&model, TF2_BANDSTOP, 0.75, 10000.0, 0.6) ==
           TF2_STATUS_OK);

    tf2_response_t low = calculate(&model, 100.0);
    tf2_response_t center = calculate(&model, 10000.0);
    tf2_response_t high = calculate(&model, 1000000.0);

    assert(nearly_equal(center.magnitude, 0.0, 1.0e-15));
    assert(nearly_equal(center.phase_rad, 0.0, 1.0e-15));
    assert(low.magnitude > center.magnitude);
    assert(high.magnitude > center.magnitude);
    assert(nearly_equal(low.magnitude, 0.75, 0.001));
    assert(nearly_equal(high.magnitude, 0.75, 0.001));
}

static void test_invalid_parameters(void)
{
    tf2_model_t model = {TF2_LOWPASS, 9.0, 9.0, 9.0};
    tf2_response_t response = {7.0, 8.0};

    assert(tf2_init(&model, TF2_LOWPASS, 1.0, 1000.0, 0.0) ==
           TF2_STATUS_INVALID_PARAMETER);
    assert(tf2_init(&model, TF2_LOWPASS, 1.0, 0.0, 0.7) ==
           TF2_STATUS_INVALID_PARAMETER);
    assert(tf2_init(&model, TF2_LOWPASS, -1.0, 1000.0, 0.7) ==
           TF2_STATUS_INVALID_PARAMETER);
    assert(tf2_init(&model, (tf2_type_t)99, 1.0, 1000.0, 0.7) ==
           TF2_STATUS_INVALID_TYPE);
    assert(tf2_init(0, TF2_LOWPASS, 1.0, 1000.0, 0.7) ==
           TF2_STATUS_NULL_POINTER);

    model.type = TF2_LOWPASS;
    model.gain = 1.0;
    model.f0_hz = 1000.0;
    model.q = 0.7;
    assert(tf2_calc_response(&model, -1.0, &response) ==
           TF2_STATUS_INVALID_PARAMETER);
    assert(response.magnitude == 7.0 && response.phase_rad == 8.0);
    assert(tf2_calc_response(0, 100.0, &response) == TF2_STATUS_NULL_POINTER);
    assert(tf2_calc_response(&model, 100.0, 0) == TF2_STATUS_NULL_POINTER);
}

int main(void)
{
    test_lowpass();
    test_highpass();
    test_bandpass();
    test_bandstop();
    test_invalid_parameters();
    puts("tf2 tests passed");
    return 0;
}
