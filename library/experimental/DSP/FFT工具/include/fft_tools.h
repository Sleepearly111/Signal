#ifndef FFT_TOOLS_H
#define FFT_TOOLS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    FFT_TOOLS_OK = 0,
    FFT_TOOLS_NULL,
    FFT_TOOLS_LENGTH_INVALID,
    FFT_TOOLS_WORKSPACE_TOO_SMALL,
    FFT_TOOLS_NON_FINITE,
    FFT_TOOLS_RANGE_ERROR
} fft_tools_status_t;

typedef enum
{
    FFT_WINDOW_HANN = 0,
    FFT_WINDOW_FLAT_TOP
} fft_window_t;

typedef struct
{
    float real;
    float imag;
} fft_complex_t;

fft_tools_status_t fft_remove_dc(float *samples, size_t length, float *mean);

fft_tools_status_t fft_apply_window(float *samples,
                                    size_t length,
                                    fft_window_t window,
                                    float *coherent_gain);

fft_tools_status_t fft_real_forward(const float *samples,
                                    size_t fft_size,
                                    fft_complex_t *spectrum,
                                    size_t spectrum_length,
                                    fft_complex_t *workspace,
                                    size_t workspace_length);

fft_tools_status_t fft_magnitude(const fft_complex_t *spectrum,
                                 size_t fft_size,
                                 float coherent_gain,
                                 float *magnitude,
                                 size_t magnitude_length);

fft_tools_status_t fft_find_peak_frequency(const float *magnitude,
                                           size_t fft_size,
                                           float sample_rate,
                                           float *frequency,
                                           float *amplitude,
                                           size_t *bin_index);

#ifdef __cplusplus
}
#endif

#endif
