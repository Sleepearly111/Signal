#include "g2026_measurement_dsp.h"

#include <math.h>
#include <string.h>

#include "g2026_sampling_quality.h"

#define G2026_PI 3.14159265358979323846F
#define G2026_RECONSTRUCTION_POINTS 1024U

static void CalculateTimeMetrics(G2026DspContext *context,
                                 const int16_t *samples,
                                 const G2026DspConfig *config,
                                 G2026DspResult *result)
{
  float sum = 0.0F;
  float sum_squares = 0.0F;
  float minimum = INFINITY;
  float maximum = -INFINITY;

  for (uint32_t i = 0U; i < G2026_SAMPLE_COUNT; ++i)
  {
    const float value =
        ((float)samples[i] - config->zero_code) *
        config->millivolts_per_code;
    context->calibrated_mv[i] = value;
    sum += value;
    sum_squares += value * value;
    minimum = fminf(minimum, value);
    maximum = fmaxf(maximum, value);
  }

  result->dc_mv = sum / (float)G2026_SAMPLE_COUNT;
  result->total_rms_mv =
      sqrtf(sum_squares / (float)G2026_SAMPLE_COUNT);
  result->time_vpp_mv = maximum - minimum;

  float ac_sum_squares = 0.0F;
  for (uint32_t i = 0U; i < G2026_SAMPLE_COUNT; ++i)
  {
    const float ac = context->calibrated_mv[i] - result->dc_mv;
    ac_sum_squares += ac * ac;
  }
  result->ac_rms_mv =
      sqrtf(ac_sum_squares / (float)G2026_SAMPLE_COUNT);
}

static void PrepareSpectrum(G2026DspContext *context,
                            const G2026DspResult *result)
{
  const float step = 2.0F * G2026_PI /
                     (float)(G2026_SAMPLE_COUNT - 1U);
  const float step_cos = cosf(step);
  const float step_sin = sinf(step);
  float cosine = 1.0F;
  float sine = 0.0F;

  for (uint32_t i = 0U; i < G2026_SAMPLE_COUNT; ++i)
  {
    const float weight = 0.5F - 0.5F * cosine;
    const float next_cosine = cosine * step_cos - sine * step_sin;
    sine = sine * step_cos + cosine * step_sin;
    cosine = next_cosine;
    context->fft_input[i] =
        (context->calibrated_mv[i] - result->dc_mv) * weight;
  }
}

static void CalculateMagnitude(G2026DspContext *context)
{
  const float coherent_gain =
      (float)(G2026_SAMPLE_COUNT - 1U) /
      (2.0F * (float)G2026_SAMPLE_COUNT);

  context->magnitude_mv[0] =
      fabsf(context->fft_output[0]) /
      ((float)G2026_SAMPLE_COUNT * coherent_gain);
  for (uint32_t k = 1U; k < G2026_SAMPLE_COUNT / 2U; ++k)
  {
    const float real = context->fft_output[2U * k];
    const float imag = context->fft_output[2U * k + 1U];
    context->magnitude_mv[k] =
        2.0F * hypotf(real, imag) /
        ((float)G2026_SAMPLE_COUNT * coherent_gain);
  }
  context->magnitude_mv[G2026_SAMPLE_COUNT / 2U] =
      fabsf(context->fft_output[1]) /
      ((float)G2026_SAMPLE_COUNT * coherent_gain);
}

static bool FitSingleComponent(const float *samples_mv,
                               float dc_mv,
                               float sample_rate_hz,
                               float frequency_hz,
                               G2026MeasuredComponent *component)
{
  const float step =
      2.0F * G2026_PI * frequency_hz / sample_rate_hz;
  const float step_cos = cosf(step);
  const float step_sin = sinf(step);
  float cosine = 1.0F;
  float sine = 0.0F;
  float xc = 0.0F;
  float xs = 0.0F;
  float cc = 0.0F;
  float ss = 0.0F;
  float cs = 0.0F;

  for (uint32_t i = 0U; i < G2026_SAMPLE_COUNT; ++i)
  {
    const float value = samples_mv[i] - dc_mv;
    const float next_cosine = cosine * step_cos - sine * step_sin;
    xc += value * cosine;
    xs += value * sine;
    cc += cosine * cosine;
    ss += sine * sine;
    cs += cosine * sine;
    sine = sine * step_cos + cosine * step_sin;
    cosine = next_cosine;
  }

  const float determinant = cc * ss - cs * cs;
  if (!isfinite(determinant) || (fabsf(determinant) < 1.0e-12F))
  {
    return false;
  }

  const float cosine_coefficient = (xc * ss - xs * cs) / determinant;
  const float sine_coefficient = (xs * cc - xc * cs) / determinant;
  component->peak_mv =
      hypotf(cosine_coefficient, sine_coefficient);
  component->phase_rad =
      atan2f(-sine_coefficient, cosine_coefficient);
  return isfinite(component->peak_mv) && isfinite(component->phase_rad);
}

static float ReconstructVpp(const G2026DspResult *result)
{
  float cosine[G2026_MAX_COMPONENTS] = {0.0F};
  float sine[G2026_MAX_COMPONENTS] = {0.0F};
  float step_cos[G2026_MAX_COMPONENTS] = {0.0F};
  float step_sin[G2026_MAX_COMPONENTS] = {0.0F};
  float minimum = INFINITY;
  float maximum = -INFINITY;

  for (uint8_t i = 0U; i < result->component_count; ++i)
  {
    const float step =
        2.0F * G2026_PI *
        (float)result->components[i].harmonic_order /
        (float)G2026_RECONSTRUCTION_POINTS;
    cosine[i] = cosf(result->components[i].phase_rad);
    sine[i] = sinf(result->components[i].phase_rad);
    step_cos[i] = cosf(step);
    step_sin[i] = sinf(step);
  }

  for (uint32_t n = 0U; n < G2026_RECONSTRUCTION_POINTS; ++n)
  {
    float value = 0.0F;
    for (uint8_t i = 0U; i < result->component_count; ++i)
    {
      const float next_cosine =
          cosine[i] * step_cos[i] - sine[i] * step_sin[i];
      value += result->components[i].peak_mv * cosine[i];
      sine[i] =
          sine[i] * step_cos[i] + cosine[i] * step_sin[i];
      cosine[i] = next_cosine;
    }
    minimum = fminf(minimum, value);
    maximum = fmaxf(maximum, value);
  }
  return maximum - minimum;
}

/*
 * The contest frequencies are constrained to the 500 Hz grid.  Fit every
 * accepted component together at that grid, so one harmonic cannot leak into
 * another component's independently fitted amplitude.
 */
static bool FitGridComponentsJointly(const G2026DspContext *context,
                                     uint32_t sample_rate_hz,
                                     G2026DspResult *result)
{
  const uint8_t column_count = (uint8_t)(1U + 2U * result->component_count);
  float augmented[1U + 2U * G2026_MAX_COMPONENTS]
                 [2U + 2U * G2026_MAX_COMPONENTS] = {{0.0F}};

  if ((result->component_count == 0U) ||
      (column_count > (1U + 2U * G2026_MAX_COMPONENTS)))
  {
    return false;
  }

  for (uint32_t n = 0U; n < G2026_SAMPLE_COUNT; ++n)
  {
    float basis[1U + 2U * G2026_MAX_COMPONENTS] = {0.0F};
    basis[0] = 1.0F;
    for (uint8_t i = 0U; i < result->component_count; ++i)
    {
      const float angle = 2.0F * G2026_PI *
                          result->components[i].frequency_hz * (float)n /
                          (float)sample_rate_hz;
      basis[1U + 2U * i] = cosf(angle);
      basis[2U + 2U * i] = sinf(angle);
    }
    for (uint8_t row = 0U; row < column_count; ++row)
    {
      augmented[row][column_count] +=
          basis[row] * context->calibrated_mv[n];
      for (uint8_t column = 0U; column < column_count; ++column)
      {
        augmented[row][column] += basis[row] * basis[column];
      }
    }
  }

  for (uint8_t pivot = 0U; pivot < column_count; ++pivot)
  {
    uint8_t best_row = pivot;
    float best_magnitude = fabsf(augmented[pivot][pivot]);
    for (uint8_t row = (uint8_t)(pivot + 1U); row < column_count; ++row)
    {
      const float magnitude = fabsf(augmented[row][pivot]);
      if (magnitude > best_magnitude)
      {
        best_magnitude = magnitude;
        best_row = row;
      }
    }
    if (!isfinite(best_magnitude) || (best_magnitude < 1.0e-9F))
    {
      return false;
    }
    if (best_row != pivot)
    {
      for (uint8_t column = pivot; column <= column_count; ++column)
      {
        const float swap = augmented[pivot][column];
        augmented[pivot][column] = augmented[best_row][column];
        augmented[best_row][column] = swap;
      }
    }
    const float divisor = augmented[pivot][pivot];
    for (uint8_t column = pivot; column <= column_count; ++column)
    {
      augmented[pivot][column] /= divisor;
    }
    for (uint8_t row = 0U; row < column_count; ++row)
    {
      if (row == pivot)
      {
        continue;
      }
      const float factor = augmented[row][pivot];
      for (uint8_t column = pivot; column <= column_count; ++column)
      {
        augmented[row][column] -= factor * augmented[pivot][column];
      }
    }
  }

  result->dc_mv = augmented[0][column_count];
  for (uint8_t i = 0U; i < result->component_count; ++i)
  {
    const float cosine_coefficient = augmented[1U + 2U * i][column_count];
    const float sine_coefficient = augmented[2U + 2U * i][column_count];
    result->components[i].grid_peak_mv =
        hypotf(cosine_coefficient, sine_coefficient);
    result->components[i].grid_phase_rad =
        atan2f(-sine_coefficient, cosine_coefficient);
    if (!isfinite(result->components[i].grid_peak_mv) ||
        !isfinite(result->components[i].grid_phase_rad))
    {
      return false;
    }
  }
  return true;
}

static void CalculateNoiseDiagnostics(const G2026DspContext *context,
                                      uint32_t sample_rate_hz,
                                      const G2026DspConfig *config,
                                      G2026DspResult *result)
{
  const float bin_width_hz =
      (float)sample_rate_hz / (float)G2026_SAMPLE_COUNT;
  const uint32_t first_bin = (uint32_t)ceilf(
      config->minimum_frequency_hz / bin_width_hz);
  const uint32_t last_bin = (uint32_t)floorf(
      config->maximum_frequency_hz / bin_width_hz);
  float noise_square_sum = 0.0F;
  float residual_square_sum = 0.0F;

  for (uint32_t bin = first_bin;
       (bin <= last_bin) && (bin < G2026_SAMPLE_COUNT / 2U);
       ++bin)
  {
    bool signal_bin = false;
    for (uint8_t i = 0U; i < result->component_count; ++i)
    {
      const float component_bin =
          result->components[i].frequency_hz / bin_width_hz;
      /* Hann main lobe and its immediate leakage neighbourhood. */
      if (fabsf((float)bin - component_bin) <= 3.0F)
      {
        signal_bin = true;
        break;
      }
    }
    if (!signal_bin)
    {
      noise_square_sum += 0.5F * context->magnitude_mv[bin] *
                          context->magnitude_mv[bin];
      ++result->fft_noise_bin_count;
    }
  }

  if (result->fft_noise_bin_count != 0U)
  {
    result->fft_noise_floor_rms_mv = sqrtf(
        noise_square_sum / (float)result->fft_noise_bin_count);
    if ((result->component_count != 0U) &&
        (result->components[0].peak_mv > 0.0F))
    {
      result->fft_noise_floor_dbc = 20.0F * log10f(
        result->fft_noise_floor_rms_mv /
          (result->components[0].grid_peak_mv / sqrtf(2.0F)));
    }
  }

  for (uint32_t n = 0U; n < G2026_SAMPLE_COUNT; ++n)
  {
    float fitted_mv = result->dc_mv;
    for (uint8_t i = 0U; i < result->component_count; ++i)
    {
      const G2026MeasuredComponent *component = &result->components[i];
      const float angle = 2.0F * G2026_PI *
                          component->frequency_hz * (float)n /
                          (float)sample_rate_hz +
                          component->grid_phase_rad;
      fitted_mv += component->grid_peak_mv * cosf(angle);
    }
    const float residual_mv = context->calibrated_mv[n] - fitted_mv;
    residual_square_sum += residual_mv * residual_mv;
  }
  result->residual_rms_mv = sqrtf(
      residual_square_sum / (float)G2026_SAMPLE_COUNT);
  if ((result->spectral_rms_mv > 0.0F) &&
      (result->residual_rms_mv > 0.0F))
  {
    result->fit_snr_db = 20.0F * log10f(
        result->spectral_rms_mv / result->residual_rms_mv);
  }
}

G2026DspStatus G2026Dsp_Init(G2026DspContext *context)
{
  if (context == NULL)
  {
    return G2026_DSP_INVALID_ARGUMENT;
  }
  memset(context, 0, sizeof(*context));
  if (arm_rfft_fast_init_f32(&context->fft, G2026_SAMPLE_COUNT) !=
      ARM_MATH_SUCCESS)
  {
    return G2026_DSP_FFT_ERROR;
  }
  context->initialized = true;
  return G2026_DSP_OK;
}

G2026DspStatus G2026Dsp_Analyze(G2026DspContext *context,
                               const int16_t *samples,
                               uint16_t sample_count,
                               uint32_t sample_rate_hz,
                               bool clipped,
                               bool overflow,
                               const G2026DspConfig *config,
                               G2026DspResult *result)
{
  G2026SpectralConfig spectral_config;
  G2026SpectralResult spectral_result;

  if ((context == NULL) || !context->initialized || (samples == NULL) ||
      (config == NULL) || (result == NULL) ||
      (sample_count != G2026_SAMPLE_COUNT) ||
      !isfinite(config->millivolts_per_code) ||
      (config->millivolts_per_code <= 0.0F))
  {
    return G2026_DSP_INVALID_ARGUMENT;
  }
  if (!isfinite(config->minimum_frequency_hz) ||
      !isfinite(config->maximum_frequency_hz) ||
      (config->maximum_frequency_hz <= 0.0F) ||
      (config->maximum_frequency_hz > (float)UINT32_MAX) ||
      !G2026SamplingQuality_IsWindowValid(
          sample_rate_hz, sample_count,
          (uint32_t)ceilf(config->maximum_frequency_hz),
          G2026_MAX_FFT_BIN_SPACING_HZ) ||
      (config->minimum_frequency_hz <= 0.0F))
  {
    return G2026_DSP_INVALID_SAMPLE_RATE;
  }
  if (clipped || overflow)
  {
    return G2026_DSP_CLIPPED;
  }

  memset(result, 0, sizeof(*result));
  CalculateTimeMetrics(context, samples, config, result);
  PrepareSpectrum(context, result);
  arm_rfft_fast_f32(&context->fft, context->fft_input,
                    context->fft_output, 0U);
  CalculateMagnitude(context);

  spectral_config.minimum_frequency_hz = config->minimum_frequency_hz;
  spectral_config.maximum_frequency_hz = config->maximum_frequency_hz;
  spectral_config.minimum_peak_mv = config->minimum_peak_mv;
  spectral_config.noise_multiplier = config->noise_multiplier;
  spectral_config.frequency_grid_hz = config->frequency_grid_hz;
  spectral_config.grid_tolerance_hz = config->grid_tolerance_hz;
  spectral_config.harmonic_tolerance_hz =
      config->harmonic_tolerance_hz;
  if (!G2026Spectral_FindPeaks(
          context->magnitude_mv,
          G2026_SAMPLE_COUNT / 2U + 1U,
          G2026_SAMPLE_COUNT,
          (float)sample_rate_hz,
          &spectral_config,
          &spectral_result))
  {
    return G2026_DSP_FFT_ERROR;
  }
  if (spectral_result.component_count == 0U)
  {
    return G2026_DSP_NO_SIGNAL;
  }

  result->component_count = spectral_result.component_count;
  result->harmonic_family_valid =
      spectral_result.harmonic_family_valid;
  result->fundamental_hz =
      spectral_result.fundamental_hz_refined;

  float spectral_square_sum = 0.0F;
  for (uint8_t i = 0U; i < result->component_count; ++i)
  {
    G2026MeasuredComponent *component = &result->components[i];
    component->raw_frequency_hz =
        spectral_result.components[i].raw_frequency_hz;
    component->frequency_hz =
        spectral_result.components[i].frequency_hz;
    component->harmonic_order =
        spectral_result.components[i].harmonic_order;
    component->harmonic_valid =
        spectral_result.components[i].harmonic_valid;
    if (!FitSingleComponent(context->calibrated_mv, result->dc_mv,
                            (float)sample_rate_hz,
                            component->raw_frequency_hz, component))
    {
      return G2026_DSP_FFT_ERROR;
    }
  }

  if (!FitGridComponentsJointly(context, sample_rate_hz, result))
  {
    return G2026_DSP_FFT_ERROR;
  }
  for (uint8_t i = 0U; i < result->component_count; ++i)
  {
    spectral_square_sum += result->components[i].grid_peak_mv *
                           result->components[i].grid_peak_mv;
  }
  result->spectral_rms_mv = sqrtf(0.5F * spectral_square_sum);
  result->rms_difference_mv =
      fabsf(result->ac_rms_mv - result->spectral_rms_mv);
  CalculateNoiseDiagnostics(context, sample_rate_hz, config, result);
  if (result->harmonic_family_valid)
  {
    G2026DspResult grid_result = *result;
    for (uint8_t i = 0U; i < grid_result.component_count; ++i)
    {
      grid_result.components[i].peak_mv =
          grid_result.components[i].grid_peak_mv;
      grid_result.components[i].phase_rad =
          grid_result.components[i].grid_phase_rad;
    }
    result->reconstructed_vpp_mv = ReconstructVpp(&grid_result);
    result->reconstructed_vpp_valid = true;
  }
  return G2026_DSP_OK;
}
