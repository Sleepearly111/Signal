#include "g2026_sampling_quality.h"

bool G2026SamplingQuality_IsWindowValid(
    uint32_t sample_rate_hz,
    uint16_t sample_count,
    uint32_t maximum_input_frequency_hz,
    uint32_t maximum_bin_spacing_hz)
{
  if ((sample_rate_hz == 0U) || (sample_count == 0U) ||
      (maximum_input_frequency_hz == 0U) ||
      (maximum_bin_spacing_hz == 0U))
  {
    return false;
  }

  if ((uint64_t)sample_rate_hz <=
      2ULL * (uint64_t)maximum_input_frequency_hz)
  {
    return false;
  }

  return (uint64_t)sample_rate_hz <=
         (uint64_t)sample_count *
             (uint64_t)maximum_bin_spacing_hz;
}
