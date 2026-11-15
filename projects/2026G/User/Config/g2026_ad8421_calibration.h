#ifndef G2026_AD8421_CALIBRATION_H
#define G2026_AD8421_CALIBRATION_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Set this to 1 only after replacing every zero gain below with the measured
 * same-channel ratio: A_with_AD8421 / A_direct.  A nominal AD8421 gain or the
 * value inferred from Rg is not acceptable here.
 */
#ifndef G2026_AD8421_GAIN_TABLE_VALID
#define G2026_AD8421_GAIN_TABLE_VALID 0U
#endif
#ifndef G2026_AD8421_ZERO_CODE_VALID
#define G2026_AD8421_ZERO_CODE_VALID 0U
#endif
#ifndef G2026_AD8421_ZERO_CODE_A
#define G2026_AD8421_ZERO_CODE_A 0.0F
#endif
#define G2026_AD8421_GAIN_POINT_COUNT 8U

typedef struct
{
  uint32_t frequency_hz;
  float direct_input_mv_per_code;
  float effective_gain;
} G2026Ad8421GainPoint;

bool G2026Ad8421Calibration_Lookup(float frequency_hz,
                                  float *direct_input_mv_per_code,
                                  float *effective_gain);

#endif
