#include "g2026_spectral_logic.h"

#include <math.h>
#include <string.h>

static float MaxFloat(float left, float right)
{
  return left > right ? left : right;
}

static void InsertCandidate(G2026SpectralPeak *candidates,
                            uint8_t *candidate_count,
                            const G2026SpectralPeak *peak)
{
  uint8_t count = *candidate_count;
  uint8_t position = count;

  if (count < G2026_MAX_PEAK_CANDIDATES)
  {
    ++count;
  }
  else if (peak->fft_peak_mv <=
           candidates[G2026_MAX_PEAK_CANDIDATES - 1U].fft_peak_mv)
  {
    return;
  }
  else
  {
    position = G2026_MAX_PEAK_CANDIDATES - 1U;
  }

  while ((position > 0U) &&
         (peak->fft_peak_mv > candidates[position - 1U].fft_peak_mv))
  {
    if (position < G2026_MAX_PEAK_CANDIDATES)
    {
      candidates[position] = candidates[position - 1U];
    }
    --position;
  }
  candidates[position] = *peak;
  *candidate_count = count;
}

static void SortComponentsByFrequency(G2026SpectralPeak *peaks, uint8_t count)
{
  for (uint8_t i = 1U; i < count; ++i)
  {
    G2026SpectralPeak value = peaks[i];
    uint8_t j = i;
    while ((j > 0U) && (value.frequency_hz < peaks[j - 1U].frequency_hz))
    {
      peaks[j] = peaks[j - 1U];
      --j;
    }
    peaks[j] = value;
  }
}

static float Square(float value)
{
  return value * value;
}

static uint8_t SelectBestHarmonicFamily(
    const G2026SpectralPeak *candidates,
    uint8_t candidate_count,
    const G2026SpectralConfig *config,
    G2026SpectralPeak *selected)
{
  float best_score = -1.0F;
  uint8_t best_count = 0U;

  for (uint8_t base_index = 0U; base_index < candidate_count; ++base_index)
  {
    G2026SpectralPeak family[G2026_MAX_COMPONENTS] = {0};
    uint8_t family_count = 1U;
    float score = Square(candidates[base_index].fft_peak_mv);

    if (!candidates[base_index].grid_valid)
    {
      continue;
    }

    family[0] = candidates[base_index];
    family[0].harmonic_order = 1U;
    family[0].harmonic_valid = true;

    for (uint8_t i = 0U; i < candidate_count; ++i)
    {
      if ((i == base_index) ||
          (candidates[i].frequency_hz <= family[0].frequency_hz) ||
          !candidates[i].grid_valid)
      {
        continue;
      }

      const long order = lroundf(candidates[i].frequency_hz /
                                 family[0].frequency_hz);
      if ((order < 2L) || (order > 255L))
      {
        continue;
      }
      const float harmonic_residual_hz =
          fabsf(candidates[i].frequency_hz -
                (float)order * family[0].frequency_hz);
      if (harmonic_residual_hz >
          config->harmonic_tolerance_hz)
      {
        continue;
      }

      G2026SpectralPeak member = candidates[i];
      member.harmonic_order = (uint8_t)order;
      member.harmonic_valid = true;

      if (family_count < G2026_MAX_COMPONENTS)
      {
        family[family_count++] = member;
        score += Square(member.fft_peak_mv);
      }
      else
      {
        uint8_t weakest = 1U;
        if (family[2].fft_peak_mv < family[1].fft_peak_mv)
        {
          weakest = 2U;
        }
        if (member.fft_peak_mv > family[weakest].fft_peak_mv)
        {
          score -= Square(family[weakest].fft_peak_mv);
          family[weakest] = member;
          score += Square(member.fft_peak_mv);
        }
      }
    }

    if ((family_count >= 2U) &&
        ((family_count > best_count) ||
         ((family_count == best_count) &&
          ((score > best_score) ||
           ((fabsf(score - best_score) < 1.0e-6F) &&
            (family[0].frequency_hz < selected[0].frequency_hz))))))
    {
      best_score = score;
      best_count = family_count;
      for (uint8_t i = 0U; i < family_count; ++i)
      {
        selected[i] = family[i];
      }
    }
  }

  if (best_count > 0U)
  {
    SortComponentsByFrequency(selected, best_count);
  }
  return best_count;
}

bool G2026Spectral_FindPeaks(const float *magnitude_mv,
                            size_t magnitude_count,
                            uint16_t fft_size,
                            float sample_rate_hz,
                            const G2026SpectralConfig *config,
                            G2026SpectralResult *result)
{
  G2026SpectralPeak candidates[G2026_MAX_PEAK_CANDIDATES] = {0};
  uint8_t candidate_count = 0U;
  uint16_t minimum_bin;
  uint16_t maximum_bin;
  float initial_mean = 0.0F;
  float clipped_mean = 0.0F;
  float bin_width_hz;

  if ((magnitude_mv == NULL) || (config == NULL) || (result == NULL) ||
      (fft_size < 8U) || (magnitude_count < ((size_t)fft_size / 2U + 1U)) ||
      !isfinite(sample_rate_hz) || (sample_rate_hz <= 0.0F) ||
      !isfinite(config->minimum_frequency_hz) ||
      !isfinite(config->maximum_frequency_hz) ||
      !isfinite(config->frequency_grid_hz) ||
      !isfinite(config->grid_tolerance_hz) ||
      (config->minimum_frequency_hz <= 0.0F) ||
      (config->maximum_frequency_hz <= config->minimum_frequency_hz) ||
      (config->frequency_grid_hz <= 0.0F) ||
      (config->grid_tolerance_hz < 0.0F) ||
      (config->maximum_frequency_hz >= 0.5F * sample_rate_hz))
  {
    return false;
  }

  memset(result, 0, sizeof(*result));
  bin_width_hz = sample_rate_hz / (float)fft_size;
  /*
   * Include the FFT bin immediately below the lower frequency boundary.
   * A legal 10 kHz tone falls at bin 20.48 for 2 MHz / 4096 points, so its
   * local maximum is bin 20 even though ceil(10 kHz / bin_width) is bin 21.
   * The interpolated frequency is range-checked below before insertion.
   */
  minimum_bin = (uint16_t)floorf(config->minimum_frequency_hz / bin_width_hz);
  maximum_bin = (uint16_t)ceilf(config->maximum_frequency_hz / bin_width_hz);
  if (maximum_bin >= (fft_size / 2U))
  {
    maximum_bin = (uint16_t)(fft_size / 2U - 1U);
  }
  if ((minimum_bin < 1U) || (maximum_bin >= (fft_size / 2U)) ||
      (minimum_bin >= maximum_bin))
  {
    return false;
  }

  for (uint16_t k = minimum_bin; k <= maximum_bin; ++k)
  {
    if (!isfinite(magnitude_mv[k]) || (magnitude_mv[k] < 0.0F))
    {
      return false;
    }
    initial_mean += magnitude_mv[k];
  }
  initial_mean /= (float)(maximum_bin - minimum_bin + 1U);

  for (uint16_t k = minimum_bin; k <= maximum_bin; ++k)
  {
    clipped_mean +=
        fminf(magnitude_mv[k], MaxFloat(3.0F * initial_mean, 1.0e-12F));
  }
  result->noise_floor_mv =
      clipped_mean / (float)(maximum_bin - minimum_bin + 1U);
  result->threshold_mv =
      MaxFloat(config->minimum_peak_mv,
               config->noise_multiplier * result->noise_floor_mv);

  for (uint16_t k = minimum_bin; k <= maximum_bin; ++k)
  {
    if ((magnitude_mv[k] >= result->threshold_mv) &&
        (magnitude_mv[k] > magnitude_mv[k - 1U]) &&
        (magnitude_mv[k] >= magnitude_mv[k + 1U]))
    {
      float delta = 0.0F;
      const float left = magnitude_mv[k - 1U];
      const float center = magnitude_mv[k];
      const float right = magnitude_mv[k + 1U];
      const float denominator = left - 2.0F * center + right;
      G2026SpectralPeak peak = {0};

      if (fabsf(denominator) > 1.0e-20F)
      {
        delta = 0.5F * (left - right) / denominator;
        if ((delta < -0.5F) || (delta > 0.5F))
        {
          delta = 0.0F;
        }
      }
      peak.raw_frequency_hz = ((float)k + delta) * bin_width_hz;
      peak.frequency_hz =
          roundf(peak.raw_frequency_hz / config->frequency_grid_hz) *
          config->frequency_grid_hz;
      peak.grid_error_hz =
          fabsf(peak.raw_frequency_hz - peak.frequency_hz);
      float grid_acceptance_hz = config->grid_tolerance_hz;
      if ((peak.frequency_hz == config->minimum_frequency_hz) ||
          (peak.frequency_hz == config->maximum_frequency_hz))
      {
        /*
         * At the legal 10 kHz/500 kHz boundaries there is no adjacent
         * in-range grid point to confuse with.  Permit up to half a 500 Hz
         * grid step so a small unreported sample-clock error does not discard
         * an otherwise valid boundary component.
         */
        grid_acceptance_hz =
            MaxFloat(grid_acceptance_hz,
                     0.5F * config->frequency_grid_hz);
      }
      peak.grid_valid =
          peak.grid_error_hz <= grid_acceptance_hz;
      if (!peak.grid_valid)
      {
        peak.frequency_hz = peak.raw_frequency_hz;
      }
      if ((peak.frequency_hz < config->minimum_frequency_hz) ||
          (peak.frequency_hz > config->maximum_frequency_hz))
      {
        continue;
      }
      peak.fft_peak_mv = center;
      peak.bin = k;
      InsertCandidate(candidates, &candidate_count, &peak);
    }
  }

  result->candidate_count = candidate_count;
  result->component_count = SelectBestHarmonicFamily(
      candidates, candidate_count, config, result->components);
  result->harmonic_family_valid = result->component_count >= 2U;

  if (result->component_count == 0U)
  {
    result->component_count =
        candidate_count < G2026_MAX_COMPONENTS
            ? candidate_count
            : G2026_MAX_COMPONENTS;
    for (uint8_t i = 0U; i < result->component_count; ++i)
    {
      result->components[i] = candidates[i];
      result->components[i].harmonic_valid = false;
    }
    SortComponentsByFrequency(result->components, result->component_count);
  }

  if (result->component_count == 0U)
  {
    return true;
  }

  result->fundamental_hz_raw =
      result->components[0].raw_frequency_hz;
  result->fundamental_hz_refined =
      result->components[0].frequency_hz;
  return true;
}
