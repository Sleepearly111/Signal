#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "g2026_spectral_logic.h"

#define FFT_SIZE 4096U
#define MAGNITUDE_COUNT (FFT_SIZE / 2U + 1U)

static void PutPeak(float *magnitude, float frequency_hz, float amplitude)
{
  const float bin_width = 2000000.0F / (float)FFT_SIZE;
  const float exact_bin = frequency_hz / bin_width;
  const unsigned int bin = (unsigned int)lroundf(exact_bin);
  const float fractional_bin = exact_bin - (float)bin;
  const float curvature = amplitude * 0.25F;

  /*
   * Build a parabola whose three-bin interpolation returns frequency_hz.
   * Fixed asymmetric neighbours made the old test compiler-dependent and,
   * for 10.5 kHz, represented about 10.77 kHz rather than the requested
   * 10.5 kHz tone.
   */
  magnitude[bin - 1U] =
      amplitude - curvature * (-1.0F - fractional_bin) *
                      (-1.0F - fractional_bin);
  magnitude[bin] =
      amplitude - curvature * fractional_bin * fractional_bin;
  magnitude[bin + 1U] =
      amplitude - curvature * (1.0F - fractional_bin) *
                      (1.0F - fractional_bin);
}

static void PutInterpolatedPeak(float *magnitude,
                                unsigned int center_bin,
                                float fractional_bin,
                                float amplitude)
{
  const float curvature = amplitude * 0.25F;
  magnitude[center_bin - 1U] =
      amplitude - curvature * (-1.0F - fractional_bin) *
                      (-1.0F - fractional_bin);
  magnitude[center_bin] =
      amplitude - curvature * fractional_bin * fractional_bin;
  magnitude[center_bin + 1U] =
      amplitude - curvature * (1.0F - fractional_bin) *
                      (1.0F - fractional_bin);
}

int main(void)
{
  float magnitude[MAGNITUDE_COUNT];
  G2026SpectralConfig config = {
      .minimum_frequency_hz = 10000.0F,
      .maximum_frequency_hz = 500000.0F,
      .minimum_peak_mv = 1.0F,
      .noise_multiplier = 6.0F,
      .frequency_grid_hz = 500.0F,
      .grid_tolerance_hz = 200.0F,
      .harmonic_tolerance_hz = 200.0F,
  };
  G2026SpectralResult result;

  for (unsigned int i = 0U; i < MAGNITUDE_COUNT; ++i)
  {
    magnitude[i] = 0.05F;
  }
  PutPeak(magnitude, 10500.0F, 20.0F);
  PutPeak(magnitude, 31500.0F, 12.0F);
  PutPeak(magnitude, 42000.0F, 8.0F);
  PutPeak(magnitude, 97000.0F, 18.0F);
  PutPeak(magnitude, 110500.0F, 16.0F);

  assert(G2026Spectral_FindPeaks(magnitude, MAGNITUDE_COUNT, FFT_SIZE,
                                2000000.0F, &config, &result));
  assert(result.component_count == 3U);
  assert(result.candidate_count >= 5U);
  assert(result.harmonic_family_valid);
  assert(result.components[0].harmonic_order == 1U);
  assert(result.components[1].harmonic_order == 3U);
  assert(result.components[2].harmonic_order == 4U);
  assert(result.fundamental_hz_refined == 10500.0F);
  assert(result.components[0].frequency_hz == 10500.0F);

  memset(magnitude, 0, sizeof(magnitude));
  PutPeak(magnitude, 100000.0F, 10.0F);
  PutPeak(magnitude, 173000.0F, 9.0F);
  assert(G2026Spectral_FindPeaks(magnitude, MAGNITUDE_COUNT, FFT_SIZE,
                                2000000.0F, &config, &result));
  assert(result.component_count == 2U);
  assert(!result.harmonic_family_valid);
  assert(result.fundamental_hz_refined == result.fundamental_hz_raw);

  memset(magnitude, 0, sizeof(magnitude));
  PutInterpolatedPeak(magnitude, 20U, 0.48F, 20.0F);
  PutPeak(magnitude, 250000.0F, 10.0F);
  PutPeak(magnitude, 500000.0F, 6.0F);
  assert(G2026Spectral_FindPeaks(magnitude, MAGNITUDE_COUNT, FFT_SIZE,
                                2000000.0F, &config, &result));
  assert(result.component_count == 3U);
  assert(result.harmonic_family_valid);
  assert(result.components[0].bin == 20U);
  assert(result.components[0].frequency_hz == 10000.0F);
  assert(result.components[1].harmonic_order == 25U);
  assert(result.components[2].harmonic_order == 50U);

  memset(magnitude, 0, sizeof(magnitude));
  PutPeak(magnitude, 25000.0F, 20.0F);
  PutPeak(magnitude, 250000.0F, 10.0F);
  PutInterpolatedPeak(magnitude, 1024U, 0.46F, 6.0F);
  assert(G2026Spectral_FindPeaks(magnitude, MAGNITUDE_COUNT, FFT_SIZE,
                                2000000.0F, &config, &result));
  assert(result.component_count == 3U);
  assert(result.harmonic_family_valid);
  assert(result.components[2].raw_frequency_hz > 500000.0F);
  assert(result.components[2].frequency_hz == 500000.0F);
  assert(result.components[2].harmonic_order == 20U);

  puts("g2026 spectral logic tests passed");
  return 0;
}
