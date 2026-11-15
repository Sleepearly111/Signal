#include "g2026_ad8421_calibration.h"

#include <math.h>
#include <stddef.h>

#if G2026_AD8421_GAIN_TABLE_VALID
static const G2026Ad8421GainPoint gain_points[
    G2026_AD8421_GAIN_POINT_COUNT] = {
    {10000U, 0.0F, 0.0F},
    {20000U, 0.0F, 0.0F},
    {50000U, 0.0F, 0.0F},
    {100000U, 0.0F, 0.0F},
    {200000U, 0.0F, 0.0F},
    {300000U, 0.0F, 0.0F},
    {400000U, 0.0F, 0.0F},
    {500000U, 0.0F, 0.0F},
};
#endif

bool G2026Ad8421Calibration_Lookup(float frequency_hz,
                                  float *direct_input_mv_per_code,
                                  float *effective_gain)
{
#if !G2026_AD8421_GAIN_TABLE_VALID || !G2026_AD8421_ZERO_CODE_VALID
  (void)frequency_hz;
  (void)direct_input_mv_per_code;
  (void)effective_gain;
  return false;
#else
  if ((direct_input_mv_per_code == NULL) || (effective_gain == NULL) ||
      !isfinite(frequency_hz) ||
      (frequency_hz < (float)gain_points[0].frequency_hz) ||
      (frequency_hz > (float)gain_points[G2026_AD8421_GAIN_POINT_COUNT - 1U]
                          .frequency_hz))
  {
    return false;
  }
  for (uint8_t i = 0U; i < G2026_AD8421_GAIN_POINT_COUNT; ++i)
  {
    if (frequency_hz <= (float)gain_points[i].frequency_hz)
    {
      if (i == 0U)
      {
        *direct_input_mv_per_code =
            gain_points[0].direct_input_mv_per_code;
        *effective_gain = gain_points[0].effective_gain;
      }
      else
      {
        const G2026Ad8421GainPoint *left = &gain_points[i - 1U];
        const G2026Ad8421GainPoint *right = &gain_points[i];
        const float fraction =
            (frequency_hz - (float)left->frequency_hz) /
            (float)(right->frequency_hz - left->frequency_hz);
        *direct_input_mv_per_code = left->direct_input_mv_per_code +
            fraction * (right->direct_input_mv_per_code -
                        left->direct_input_mv_per_code);
        *effective_gain = left->effective_gain +
                          fraction * (right->effective_gain -
                                      left->effective_gain);
      }
      return isfinite(*direct_input_mv_per_code) &&
             (*direct_input_mv_per_code > 0.0F) &&
             isfinite(*effective_gain) && (*effective_gain > 1.0F);
    }
  }
  return false;
#endif
}
