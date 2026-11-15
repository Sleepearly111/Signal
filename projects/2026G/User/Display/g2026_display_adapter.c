#include "g2026_display_adapter.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tjc_display.h"
#include "tjc_display_config.h"

/* The 1P/3P buttons must rebuild the waveform from the same completed
 * acquisition. Keep a display-owned voltage snapshot and its final text.
 * This CPU-only snapshot belongs in CCMRAM; no peripheral DMA accesses it. */
#if defined(__GNUC__)
#define G2026_DISPLAY_CCMRAM __attribute__((section(".ccmram"), aligned(4)))
#else
#define G2026_DISPLAY_CCMRAM
#endif
static float s_latest_voltage_samples_v[TJC_DISPLAY_TIME_SOURCE_SAMPLE_COUNT]
    G2026_DISPLAY_CCMRAM;
static TjcDisplayTimeSubmission s_latest_time_submission;
static TjcDisplaySpectrumSubmission s_latest_spectrum_submission;
static uint8_t s_latest_time_valid;

static long G2026Display_ToX100(float value)
{
  return lroundf(value * 100.0F);
}

static void G2026Display_FormatFrequency(char *text, size_t size,
                                        const char *label, float frequency_hz)
{
  const long frequency_x10_khz = lroundf(frequency_hz / 100.0F);
  (void)snprintf(text, size, "%s%ld.%01ldkHz",
                 label,
                 frequency_x10_khz / 10L,
                 labs(frequency_x10_khz % 10L));
}

static void G2026Display_FormatMillivolts(char *text, size_t size,
                                         const char *label, float value_mv)
{
  const long value_x100 = G2026Display_ToX100(value_mv);
  (void)snprintf(text, size, "%s%ld.%02ldmV",
                 label,
                 value_x100 / 100L,
                 labs(value_x100 % 100L));
}

static void G2026Display_FormatTimeComponentVpp(
    char *text, size_t size,
    const G2026MeasuredComponent *component,
    float apparatus_input_scale)
{
  const unsigned int order =
      component->harmonic_order == 0U ? 1U : component->harmonic_order;
  const long frequency_x10_khz =
      lroundf(component->frequency_hz / 100.0F);
  const long vpp_x100 = G2026Display_ToX100(
      2.0F * component->grid_peak_mv * apparatus_input_scale);

  (void)snprintf(text, size,
                 "H%u %ld.%01ldkHz\r\n%ld.%02ldmVpp",
                 order,
                 frequency_x10_khz / 10L,
                 labs(frequency_x10_khz % 10L),
                 vpp_x100 / 100L,
                 labs(vpp_x100 % 100L));
}

static void G2026Display_FormatSpectrumComponentAmplitude(
    char *text, size_t size,
    const G2026MeasuredComponent *component,
    float apparatus_input_scale)
{
  const unsigned int order =
      component->harmonic_order == 0U ? 1U : component->harmonic_order;
  const long frequency_x10_khz =
      lroundf(component->frequency_hz / 100.0F);
  const long amplitude_x100 = G2026Display_ToX100(
      component->grid_peak_mv * apparatus_input_scale);

  (void)snprintf(text, size,
                 "H%u %ld.%01ldkHz\r\n%ld.%02ldmVpk",
                 order,
                 frequency_x10_khz / 10L,
                 labs(frequency_x10_khz % 10L),
                 amplitude_x100 / 100L,
                 labs(amplitude_x100 % 100L));
}

void G2026Display_Init(UART_HandleTypeDef *uart)
{
  s_latest_time_valid = 0U;
  (void)memset(&s_latest_time_submission, 0,
               sizeof(s_latest_time_submission));
  (void)memset(&s_latest_spectrum_submission, 0,
               sizeof(s_latest_spectrum_submission));
  TjcDisplay_Init(uart);
}

void G2026Display_Process(void)
{
  TjcTimePeriodMode requested_period;

  TjcDisplay_Service();

  /* Rebuild 1P/3P from the latest completed acquisition, never from a window
   * that FPGA reception or DSP is still modifying. */
  if (TjcDisplay_TakeTimePeriodRequest(&requested_period))
  {
    (void)requested_period;
    if ((s_latest_time_valid != 0U) &&
        (TjcDisplay_SubmitTime(&s_latest_time_submission) ==
         TJC_TIME_STATUS_OK))
    {
      TjcDisplay_RequestTimeWaveformRefresh();
    }
  }
}

void G2026Display_SubmitMeasurement(
    uint32_t acquisition_id,
    const float *calibrated_samples_mv,
    uint16_t sample_count,
    float corrected_sample_rate_hz,
    const G2026DspResult *result,
    float apparatus_input_scale)
{
  TjcDisplayTimeSubmission *time_submission = &s_latest_time_submission;
  TjcDisplaySpectrumSubmission *spectrum_submission =
      &s_latest_spectrum_submission;
  float total_vpp_mv;
  uint8_t line_count;

  if ((calibrated_samples_mv == NULL) || (result == NULL) ||
      (sample_count != TJC_DISPLAY_TIME_SOURCE_SAMPLE_COUNT) ||
      !isfinite(corrected_sample_rate_hz) ||
      (corrected_sample_rate_hz <= 0.0F) ||
      !isfinite(apparatus_input_scale) ||
      (apparatus_input_scale <= 0.0F))
  {
    return;
  }

  total_vpp_mv = (result->reconstructed_vpp_valid
                      ? result->reconstructed_vpp_mv
                      : result->time_vpp_mv) *
                 apparatus_input_scale;

  for (uint16_t index = 0U; index < sample_count; ++index)
  {
    s_latest_voltage_samples_v[index] =
        calibrated_samples_mv[index] * apparatus_input_scale / 1000.0F;
  }

  (void)memset(time_submission, 0, sizeof(*time_submission));
  time_submission->acquisition_id = acquisition_id;
  time_submission->valid = 1U;
  time_submission->voltage_samples_v = s_latest_voltage_samples_v;
  time_submission->sample_count = sample_count;
  time_submission->sample_rate_hz = corrected_sample_rate_hz;
  time_submission->fundamental_hz = result->fundamental_hz;
  G2026Display_FormatMillivolts(
      time_submission->text.rows[0],
      sizeof(time_submission->text.rows[0]), "Upp ",
      total_vpp_mv);
  G2026Display_FormatMillivolts(
      time_submission->text.rows[1],
      sizeof(time_submission->text.rows[1]), "Urms ",
      result->spectral_rms_mv * apparatus_input_scale);
  G2026Display_FormatFrequency(
      time_submission->text.rows[2],
      sizeof(time_submission->text.rows[2]), "f1 ",
      result->fundamental_hz);

  line_count = result->component_count;
  if (line_count > TJC_DISPLAY_MAX_SPECTRUM_LINES)
  {
    line_count = TJC_DISPLAY_MAX_SPECTRUM_LINES;
  }
  for (uint8_t index = 0U; index < TJC_DISPLAY_MAX_SPECTRUM_LINES; ++index)
  {
    if (index < line_count)
    {
      G2026Display_FormatTimeComponentVpp(
          time_submission->text.rows[index + 3U],
          sizeof(time_submission->text.rows[index + 3U]),
          &result->components[index], apparatus_input_scale);
    }
    else
    {
      (void)snprintf(time_submission->text.rows[index + 3U],
                     sizeof(time_submission->text.rows[index + 3U]), "--");
    }
  }
  s_latest_time_valid =
      (TjcDisplay_SubmitTime(time_submission) == TJC_TIME_STATUS_OK) ? 1U : 0U;

  (void)memset(spectrum_submission, 0, sizeof(*spectrum_submission));
  spectrum_submission->acquisition_id = acquisition_id;
  spectrum_submission->valid = line_count > 0U ? 1U : 0U;
  spectrum_submission->line_count = line_count;
  spectrum_submission->full_scale_vpk = TJC_SPECTRUM_FULL_SCALE_VPK;
  G2026Display_FormatFrequency(
      spectrum_submission->text.rows[0],
      sizeof(spectrum_submission->text.rows[0]), "f1 ",
      result->fundamental_hz);
  (void)snprintf(spectrum_submission->text.rows[1],
                 sizeof(spectrum_submission->text.rows[1]),
                 "Lines %u", (unsigned int)line_count);

  for (uint8_t index = 0U; index < TJC_DISPLAY_MAX_SPECTRUM_LINES; ++index)
  {
    if (index < line_count)
    {
      const G2026MeasuredComponent *component = &result->components[index];
      spectrum_submission->lines[index].harmonic_order =
          component->harmonic_order == 0U ? 1U : component->harmonic_order;
      spectrum_submission->lines[index].frequency_hz =
          component->frequency_hz;
      spectrum_submission->lines[index].amplitude_vpk =
          component->grid_peak_mv * apparatus_input_scale / 1000.0F;
      G2026Display_FormatSpectrumComponentAmplitude(
          spectrum_submission->text.rows[index + 2U],
          sizeof(spectrum_submission->text.rows[index + 2U]),
          component, apparatus_input_scale);
    }
    else
    {
      (void)snprintf(spectrum_submission->text.rows[index + 2U],
                     sizeof(spectrum_submission->text.rows[index + 2U]), "--");
    }
  }
  (void)TjcDisplay_SubmitSpectrum(spectrum_submission);
}

void G2026Display_OnUartRxComplete(UART_HandleTypeDef *uart)
{
  TjcDisplay_OnUartRxComplete(uart);
}

void G2026Display_OnUartTxComplete(UART_HandleTypeDef *uart)
{
  TjcDisplay_OnUartTxComplete(uart);
}

void G2026Display_OnUartError(UART_HandleTypeDef *uart)
{
  TjcDisplay_OnUartError(uart);
}
