#ifndef G2026_SAMPLING_QUALITY_H
#define G2026_SAMPLING_QUALITY_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Validates both strict Nyquist margin and the maximum FFT-bin spacing.
 * Integer cross-products avoid accepting a rate because of truncating
 * sample_rate_hz / sample_count.
 */
bool G2026SamplingQuality_IsWindowValid(
    uint32_t sample_rate_hz,
    uint16_t sample_count,
    uint32_t maximum_input_frequency_hz,
    uint32_t maximum_bin_spacing_hz);

#endif
