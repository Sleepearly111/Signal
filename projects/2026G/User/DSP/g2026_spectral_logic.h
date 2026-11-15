#ifndef G2026_SPECTRAL_LOGIC_H
#define G2026_SPECTRAL_LOGIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define G2026_MAX_COMPONENTS 3U
#define G2026_MAX_PEAK_CANDIDATES 8U

typedef struct
{
  float raw_frequency_hz;
  float frequency_hz;
  float grid_error_hz;
  float fft_peak_mv;
  uint16_t bin;
  uint8_t harmonic_order;
  bool grid_valid;
  bool harmonic_valid;
} G2026SpectralPeak;

typedef struct
{
  float minimum_frequency_hz;
  float maximum_frequency_hz;
  float minimum_peak_mv;
  float noise_multiplier;
  float frequency_grid_hz;
  float grid_tolerance_hz;
  float harmonic_tolerance_hz;
} G2026SpectralConfig;

typedef struct
{
  G2026SpectralPeak components[G2026_MAX_COMPONENTS];
  uint8_t component_count;
  uint8_t candidate_count;
  float noise_floor_mv;
  float threshold_mv;
  float fundamental_hz_raw;
  float fundamental_hz_refined;
  bool harmonic_family_valid;
} G2026SpectralResult;

bool G2026Spectral_FindPeaks(const float *magnitude_mv,
                            size_t magnitude_count,
                            uint16_t fft_size,
                            float sample_rate_hz,
                            const G2026SpectralConfig *config,
                            G2026SpectralResult *result);

#endif
