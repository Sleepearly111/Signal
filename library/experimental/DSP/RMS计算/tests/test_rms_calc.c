#include "rms_calc.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static int nearly_equal(float actual, float expected, float epsilon)
{
    return fabsf(actual - expected) <= epsilon;
}

static void test_dc(void)
{
    const float samples[] = {1.0F, 1.0F, 1.0F, 1.0F};
    float result = -1.0F;

    assert(rms_calc_mean(samples, 4U, &result) == RMS_OK);
    assert(result == 1.0F);
    assert(rms_calc_total(samples, 4U, &result) == RMS_OK);
    assert(result == 1.0F);
    assert(rms_calc_ac(samples, 4U, &result) == RMS_OK);
    assert(result == 0.0F);
}

static void test_symmetric(void)
{
    const float samples[] = {-1.0F, 1.0F, -1.0F, 1.0F};
    float result = -1.0F;

    assert(rms_calc_mean(samples, 4U, &result) == RMS_OK);
    assert(nearly_equal(result, 0.0F, 1.0e-6F));
    assert(rms_calc_ac(samples, 4U, &result) == RMS_OK);
    assert(nearly_equal(result, 1.0F, 1.0e-6F));
}

static void test_sine_samples(void)
{
    const float root_half = 0.70710678F;
    const float samples[] = {
        0.0F, root_half, 1.0F, root_half,
        0.0F, -root_half, -1.0F, -root_half
    };
    float result = -1.0F;

    assert(rms_calc_total(samples, 8U, &result) == RMS_OK);
    assert(nearly_equal(result, root_half, 1.0e-6F));
    assert(rms_calc_ac(samples, 8U, &result) == RMS_OK);
    assert(nearly_equal(result, root_half, 1.0e-6F));
}

static void test_single_sample(void)
{
    const float samples[] = {5.0F};
    float result = -1.0F;

    assert(rms_calc_mean(samples, 1U, &result) == RMS_OK);
    assert(result == 5.0F);
    assert(rms_calc_total(samples, 1U, &result) == RMS_OK);
    assert(result == 5.0F);
    assert(rms_calc_ac(samples, 1U, &result) == RMS_OK);
    assert(result == 0.0F);
}

static void test_errors_preserve_output(void)
{
    const float valid[] = {1.0F, 2.0F};
    const float with_nan[] = {1.0F, NAN};
    const float with_infinity[] = {1.0F, INFINITY};
    float output = 123.0F;

    assert(rms_calc_mean(0, 2U, &output) == RMS_NULL);
    assert(output == 123.0F);
    assert(rms_calc_total(valid, 0U, &output) == RMS_LENGTH_INVALID);
    assert(output == 123.0F);
    assert(rms_calc_ac(with_nan, 2U, &output) == RMS_NON_FINITE);
    assert(output == 123.0F);
    assert(rms_calc_total(with_infinity, 2U, &output) == RMS_NON_FINITE);
    assert(output == 123.0F);
    assert(rms_calc_mean(valid, 2U, 0) == RMS_NULL);
    assert(rms_calc_total(valid, 2U, 0) == RMS_NULL);
    assert(rms_calc_ac(valid, 2U, 0) == RMS_NULL);
}

int main(void)
{
    test_dc();
    test_symmetric();
    test_sine_samples();
    test_single_sample();
    test_errors_preserve_output();
    puts("rms_calc tests passed");
    return 0;
}
