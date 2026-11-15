#include "fft_tools.h"

#include <math.h>

#define FFT_TOOLS_PI 3.14159265358979323846F

static int is_power_of_two(size_t value)
{
    return value >= 2U && (value & (value - 1U)) == 0U;
}

static void swap_complex(fft_complex_t *left, fft_complex_t *right)
{
    const fft_complex_t temporary = *left;
    *left = *right;
    *right = temporary;
}

fft_tools_status_t fft_remove_dc(float *samples, size_t length, float *mean)
{
    float average = 0.0F;
    size_t i;

    if (samples == 0 || mean == 0) {
        return FFT_TOOLS_NULL;
    }
    if (length == 0U) {
        return FFT_TOOLS_LENGTH_INVALID;
    }
    for (i = 0U; i < length; ++i) {
        if (!isfinite(samples[i])) {
            return FFT_TOOLS_NON_FINITE;
        }
        average += (samples[i] - average) / (float)(i + 1U);
        if (!isfinite(average)) {
            return FFT_TOOLS_NON_FINITE;
        }
    }
    for (i = 0U; i < length; ++i) {
        samples[i] -= average;
    }
    *mean = average;
    return FFT_TOOLS_OK;
}

fft_tools_status_t fft_apply_window(float *samples,
                                    size_t length,
                                    fft_window_t window,
                                    float *coherent_gain)
{
    float gain_sum = 0.0F;
    size_t i;

    if (samples == 0 || coherent_gain == 0) {
        return FFT_TOOLS_NULL;
    }
    if (length < 2U) {
        return FFT_TOOLS_LENGTH_INVALID;
    }
    if (window != FFT_WINDOW_HANN && window != FFT_WINDOW_FLAT_TOP) {
        return FFT_TOOLS_RANGE_ERROR;
    }
    for (i = 0U; i < length; ++i) {
        if (!isfinite(samples[i])) {
            return FFT_TOOLS_NON_FINITE;
        }
    }

    for (i = 0U; i < length; ++i) {
        const float angle = 2.0F * FFT_TOOLS_PI * (float)i /
                            (float)(length - 1U);
        float weight;

        if (window == FFT_WINDOW_HANN) {
            weight = 0.5F - 0.5F * cosf(angle);
        } else {
            weight = 0.21557895F
                   - 0.41663158F * cosf(angle)
                   + 0.277263158F * cosf(2.0F * angle)
                   - 0.083578947F * cosf(3.0F * angle)
                   + 0.006947368F * cosf(4.0F * angle);
        }
        samples[i] *= weight;
        gain_sum += weight;
    }

    *coherent_gain = gain_sum / (float)length;
    return FFT_TOOLS_OK;
}

fft_tools_status_t fft_real_forward(const float *samples,
                                    size_t fft_size,
                                    fft_complex_t *spectrum,
                                    size_t spectrum_length,
                                    fft_complex_t *workspace,
                                    size_t workspace_length)
{
    size_t i;
    size_t j;
    size_t stage_length;

    if (samples == 0 || spectrum == 0 || workspace == 0) {
        return FFT_TOOLS_NULL;
    }
    if (!is_power_of_two(fft_size)) {
        return FFT_TOOLS_LENGTH_INVALID;
    }
    if (spectrum_length < fft_size / 2U + 1U || workspace_length < fft_size) {
        return FFT_TOOLS_WORKSPACE_TOO_SMALL;
    }

    for (i = 0U; i < fft_size; ++i) {
        if (!isfinite(samples[i])) {
            return FFT_TOOLS_NON_FINITE;
        }
        workspace[i].real = samples[i];
        workspace[i].imag = 0.0F;
    }

    j = 0U;
    for (i = 1U; i < fft_size; ++i) {
        size_t bit = fft_size >> 1U;
        while ((j & bit) != 0U) {
            j ^= bit;
            bit >>= 1U;
        }
        j ^= bit;
        if (i < j) {
            swap_complex(&workspace[i], &workspace[j]);
        }
    }

    for (stage_length = 2U; stage_length <= fft_size; stage_length <<= 1U) {
        const size_t half = stage_length / 2U;
        const float step = -2.0F * FFT_TOOLS_PI / (float)stage_length;
        size_t base;

        for (base = 0U; base < fft_size; base += stage_length) {
            size_t k;
            for (k = 0U; k < half; ++k) {
                const float angle = step * (float)k;
                const float wr = cosf(angle);
                const float wi = sinf(angle);
                const fft_complex_t even = workspace[base + k];
                const fft_complex_t odd = workspace[base + k + half];
                const float tr = wr * odd.real - wi * odd.imag;
                const float ti = wr * odd.imag + wi * odd.real;

                workspace[base + k].real = even.real + tr;
                workspace[base + k].imag = even.imag + ti;
                workspace[base + k + half].real = even.real - tr;
                workspace[base + k + half].imag = even.imag - ti;
            }
        }
        if (stage_length == fft_size) {
            break;
        }
    }

    for (i = 0U; i <= fft_size / 2U; ++i) {
        spectrum[i] = workspace[i];
    }
    return FFT_TOOLS_OK;
}

fft_tools_status_t fft_magnitude(const fft_complex_t *spectrum,
                                 size_t fft_size,
                                 float coherent_gain,
                                 float *magnitude,
                                 size_t magnitude_length)
{
    size_t k;

    if (spectrum == 0 || magnitude == 0) {
        return FFT_TOOLS_NULL;
    }
    if (!is_power_of_two(fft_size)) {
        return FFT_TOOLS_LENGTH_INVALID;
    }
    if (magnitude_length < fft_size / 2U + 1U) {
        return FFT_TOOLS_WORKSPACE_TOO_SMALL;
    }
    if (!isfinite(coherent_gain) || coherent_gain <= 0.0F) {
        return FFT_TOOLS_RANGE_ERROR;
    }
    for (k = 0U; k <= fft_size / 2U; ++k) {
        if (!isfinite(spectrum[k].real) || !isfinite(spectrum[k].imag)) {
            return FFT_TOOLS_NON_FINITE;
        }
    }

    for (k = 0U; k <= fft_size / 2U; ++k) {
        const float scale = (k == 0U || k == fft_size / 2U) ? 1.0F : 2.0F;
        magnitude[k] = hypotf(spectrum[k].real, spectrum[k].imag) * scale /
                       ((float)fft_size * coherent_gain);
    }
    return FFT_TOOLS_OK;
}

fft_tools_status_t fft_find_peak_frequency(const float *magnitude,
                                           size_t fft_size,
                                           float sample_rate,
                                           float *frequency,
                                           float *amplitude,
                                           size_t *bin_index)
{
    const size_t nyquist = fft_size / 2U;
    size_t peak = 1U;
    float delta = 0.0F;
    float peak_amplitude;
    size_t k;

    if (magnitude == 0 || frequency == 0) {
        return FFT_TOOLS_NULL;
    }
    if (!is_power_of_two(fft_size)) {
        return FFT_TOOLS_LENGTH_INVALID;
    }
    if (!isfinite(sample_rate) || sample_rate <= 0.0F) {
        return FFT_TOOLS_RANGE_ERROR;
    }
    for (k = 0U; k <= nyquist; ++k) {
        if (!isfinite(magnitude[k]) || magnitude[k] < 0.0F) {
            return FFT_TOOLS_NON_FINITE;
        }
    }

    for (k = 2U; k <= nyquist; ++k) {
        if (magnitude[k] > magnitude[peak]) {
            peak = k;
        }
    }
    peak_amplitude = magnitude[peak];
    if (peak > 1U && peak < nyquist) {
        const float left = magnitude[peak - 1U];
        const float center = magnitude[peak];
        const float right = magnitude[peak + 1U];
        const float denominator = left - 2.0F * center + right;
        if (fabsf(denominator) > 1.0e-20F) {
            delta = 0.5F * (left - right) / denominator;
            if (delta < -0.5F || delta > 0.5F) {
                delta = 0.0F;
            }
        }
    }

    *frequency = ((float)peak + delta) * sample_rate / (float)fft_size;
    if (amplitude != 0) {
        *amplitude = peak_amplitude;
    }
    if (bin_index != 0) {
        *bin_index = peak;
    }
    return FFT_TOOLS_OK;
}
