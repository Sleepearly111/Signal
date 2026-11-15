#ifndef G2026_DUAL_GAIN_H
#define G2026_DUAL_GAIN_H

#include <stdbool.h>
#include <stdint.h>

#include "g2026_measurement_dsp.h"

typedef enum
{
  G2026_DUAL_GAIN_OK = 0,
  G2026_DUAL_GAIN_INVALID_ARGUMENT,
  G2026_DUAL_GAIN_NO_MATCHED_COMPONENT,
  G2026_DUAL_GAIN_INPUT_TOO_SMALL
} G2026DualGainStatus;

typedef struct
{
  float frequency_hz;
  float input_peak_mv;
  float output_peak_mv;
  float gain;
  float corrected_input_peak_mv;
  uint8_t harmonic_order;
  bool valid;
} G2026DualGainComponent;

typedef struct
{
  float rms_gain;
  G2026DualGainComponent components[G2026_MAX_COMPONENTS];
  uint8_t component_count;
} G2026DualGainResult;

G2026DualGainStatus G2026DualGain_Calculate(
    const G2026DspResult *input,
    const G2026DspResult *output,
    float minimum_input_peak_mv,
    float frequency_tolerance_hz,
    G2026DualGainResult *result);

#endif
