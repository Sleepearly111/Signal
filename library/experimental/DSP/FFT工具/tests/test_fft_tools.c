#include "fft_tools.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

#define TEST_PI 3.14159265358979323846F
#define TEST_N 64U

static int nearly_equal(float actual, float expected, float tolerance)
{
    return fabsf(actual - expected) <= tolerance;
}

static void test_remove_dc(void)
{
    float samples[] = {2.0F, 4.0F, 6.0F, 8.0F};
    float mean = -1.0F;

    assert(fft_remove_dc(samples, 4U, &mean) == FFT_TOOLS_OK);
    assert(nearly_equal(mean, 5.0F, 1.0e-6F));
    assert(nearly_equal(samples[0], -3.0F, 1.0e-6F));
    assert(nearly_equal(samples[3], 3.0F, 1.0e-6F));
}

static void test_windows(void)
{
    float hann[8] = {1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F};
    float flat[8] = {1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F};
    float gain = -1.0F;

    assert(fft_apply_window(hann, 8U, FFT_WINDOW_HANN, &gain) ==
           FFT_TOOLS_OK);
    assert(nearly_equal(hann[0], 0.0F, 1.0e-6F));
    assert(nearly_equal(hann[7], 0.0F, 1.0e-6F));
    assert(gain > 0.0F && gain < 1.0F);

    assert(fft_apply_window(flat, 8U, FFT_WINDOW_FLAT_TOP, &gain) ==
           FFT_TOOLS_OK);
    assert(isfinite(gain) && gain > 0.0F);
}

static void test_fft_magnitude_and_peak(void)
{
    float samples[TEST_N];
    fft_complex_t spectrum[TEST_N / 2U + 1U];
    fft_complex_t workspace[TEST_N];
    float magnitude[TEST_N / 2U + 1U];
    float frequency = -1.0F;
    float amplitude = -1.0F;
    size_t bin = 0U;
    size_t i;

    for (i = 0U; i < TEST_N; ++i) {
        samples[i] = 1.5F * sinf(2.0F * TEST_PI * 8.0F * (float)i /
                                 (float)TEST_N);
    }
    assert(fft_real_forward(samples, TEST_N, spectrum,
                            TEST_N / 2U + 1U, workspace, TEST_N) ==
           FFT_TOOLS_OK);
    assert(fft_magnitude(spectrum, TEST_N, 1.0F, magnitude,
                         TEST_N / 2U + 1U) == FFT_TOOLS_OK);
    assert(nearly_equal(magnitude[8], 1.5F, 2.0e-5F));
    assert(fft_find_peak_frequency(magnitude, TEST_N, 6400.0F,
                                   &frequency, &amplitude, &bin) ==
           FFT_TOOLS_OK);
    assert(bin == 8U);
    assert(nearly_equal(frequency, 800.0F, 1.0e-3F));
    assert(nearly_equal(amplitude, 1.5F, 2.0e-5F));
}

static void test_invalid_inputs(void)
{
    float samples[8] = {0.0F};
    float invalid[8] = {0.0F, NAN, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F};
    fft_complex_t spectrum[5];
    fft_complex_t workspace[8];
    float magnitude[5] = {0.0F};
    float output = 123.0F;

    assert(fft_remove_dc(0, 8U, &output) == FFT_TOOLS_NULL);
    assert(output == 123.0F);
    assert(fft_remove_dc(samples, 0U, &output) == FFT_TOOLS_LENGTH_INVALID);
    assert(output == 123.0F);
    assert(fft_remove_dc(invalid, 8U, &output) == FFT_TOOLS_NON_FINITE);
    assert(output == 123.0F);
    assert(fft_real_forward(samples, 6U, spectrum, 5U, workspace, 8U) ==
           FFT_TOOLS_LENGTH_INVALID);
    assert(fft_real_forward(samples, 8U, spectrum, 4U, workspace, 8U) ==
           FFT_TOOLS_WORKSPACE_TOO_SMALL);
    assert(fft_magnitude(spectrum, 8U, 0.0F, magnitude, 5U) ==
           FFT_TOOLS_RANGE_ERROR);
    assert(fft_find_peak_frequency(magnitude, 8U, 0.0F, &output, 0, 0) ==
           FFT_TOOLS_RANGE_ERROR);
    assert(output == 123.0F);
}

int main(void)
{
    test_remove_dc();
    test_windows();
    test_fft_magnitude_and_peak();
    test_invalid_inputs();
    puts("fft_tools tests passed");
    return 0;
}
