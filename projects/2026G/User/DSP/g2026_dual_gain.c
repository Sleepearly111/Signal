#include "g2026_dual_gain.h"

#include <math.h>
#include <string.h>

static const G2026MeasuredComponent *FindOutputComponent(
    const G2026MeasuredComponent *input,
    const G2026DspResult *output,
    float frequency_tolerance_hz)
{
  const G2026MeasuredComponent *best = NULL;
  float best_error = INFINITY;

  for (uint8_t i = 0U; i < output->component_count; ++i)
  {
    const G2026MeasuredComponent *candidate = &output->components[i];
    const float error =
        fabsf(candidate->frequency_hz - input->frequency_hz);
    const bool same_order =
        input->harmonic_valid && candidate->harmonic_valid &&
        (input->harmonic_order == candidate->harmonic_order);
    if ((same_order || (error <= frequency_tolerance_hz)) &&
        (error < best_error))
    {
      best = candidate;
      best_error = error;
    }
  }
  return best;
}

G2026DualGainStatus G2026DualGain_Calculate(
    const G2026DspResult *input,
    const G2026DspResult *output,
    float minimum_input_peak_mv,
    float frequency_tolerance_hz,
    G2026DualGainResult *result)
{
  bool saw_small_input = false;

  if ((input == NULL) || (output == NULL) || (result == NULL) ||
      !isfinite(minimum_input_peak_mv) ||
      !isfinite(frequency_tolerance_hz) ||
      (minimum_input_peak_mv <= 0.0F) ||
      (frequency_tolerance_hz < 0.0F))
  {
    return G2026_DUAL_GAIN_INVALID_ARGUMENT;
  }

  memset(result, 0, sizeof(*result));
  if ((input->spectral_rms_mv >= minimum_input_peak_mv) &&
      isfinite(input->spectral_rms_mv) &&
      isfinite(output->spectral_rms_mv))
  {
    result->rms_gain =
        output->spectral_rms_mv / input->spectral_rms_mv;
  }

  for (uint8_t i = 0U;
       (i < input->component_count) &&
       (result->component_count < G2026_MAX_COMPONENTS);
       ++i)
  {
    const G2026MeasuredComponent *input_component =
        &input->components[i];
    const G2026MeasuredComponent *output_component;
    G2026DualGainComponent *gain_component;

    if (!isfinite(input_component->peak_mv) ||
        (input_component->peak_mv < minimum_input_peak_mv))
    {
      saw_small_input = true;
      continue;
    }
    output_component = FindOutputComponent(
        input_component, output, frequency_tolerance_hz);
    if ((output_component == NULL) ||
        !isfinite(output_component->peak_mv) ||
        (output_component->peak_mv < 0.0F))
    {
      continue;
    }

    gain_component =
        &result->components[result->component_count++];
    gain_component->frequency_hz = input_component->frequency_hz;
    gain_component->input_peak_mv = input_component->peak_mv;
    gain_component->output_peak_mv = output_component->peak_mv;
    gain_component->gain =
        output_component->peak_mv / input_component->peak_mv;
    gain_component->corrected_input_peak_mv =
        output_component->peak_mv / gain_component->gain;
    gain_component->harmonic_order = input_component->harmonic_order;
    gain_component->valid =
        isfinite(gain_component->gain) &&
        (gain_component->gain > 0.0F);
  }

  if (result->component_count != 0U)
  {
    return G2026_DUAL_GAIN_OK;
  }
  return saw_small_input
             ? G2026_DUAL_GAIN_INPUT_TOO_SMALL
             : G2026_DUAL_GAIN_NO_MATCHED_COMPONENT;
}
