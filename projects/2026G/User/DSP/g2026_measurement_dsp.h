#ifndef G2026_MEASUREMENT_DSP_H
#define G2026_MEASUREMENT_DSP_H

#include <stdbool.h>
#include <stdint.h>

#include "arm_math.h"
#include "g2026_config.h"
#include "g2026_spectral_logic.h"

typedef enum
{
  G2026_DSP_OK = 0,
  G2026_DSP_INVALID_ARGUMENT,
  G2026_DSP_INVALID_SAMPLE_RATE,
  G2026_DSP_CLIPPED,
  G2026_DSP_FFT_ERROR,
  G2026_DSP_NO_SIGNAL
} G2026DspStatus;

typedef struct
{
  /* Measured apparatus-input scale; nominal 1x/2x/4x is never a fallback. */
  float millivolts_per_code;
  float zero_code;
  float minimum_frequency_hz;
  float maximum_frequency_hz;
  float minimum_peak_mv;
  float noise_multiplier;
  float frequency_grid_hz;
  float grid_tolerance_hz;
  float harmonic_tolerance_hz;
} G2026DspConfig;

typedef struct
{
  float raw_frequency_hz; /* Used internally for amplitude/phase fitting. */
  float frequency_hz;     /* 500 Hz grid-corrected value for display. */
  float peak_mv;          /* Peak fitted at raw_frequency_hz. */
  float grid_peak_mv;     /* Peak independently fitted at frequency_hz. */
  float phase_rad;
  float grid_phase_rad;   /* Phase from the joint grid-frequency fit. */
  uint8_t harmonic_order;
  bool harmonic_valid;
} G2026MeasuredComponent;

typedef struct
{
  float dc_mv;
  float total_rms_mv;
  float ac_rms_mv;
  float time_vpp_mv;
  float reconstructed_vpp_mv;
  float spectral_rms_mv;
  float rms_difference_mv;
  float fft_noise_floor_rms_mv; /* Mean RMS power per non-signal FFT bin. */
  float fft_noise_floor_dbc;    /* Relative to the fitted fundamental RMS. */
  float residual_rms_mv;        /* Time-domain residual after fitted components. */
  float fit_snr_db;
  uint16_t fft_noise_bin_count;
  float fundamental_hz;
  G2026MeasuredComponent components[G2026_MAX_COMPONENTS];
  uint8_t component_count;
  bool harmonic_family_valid;
  bool reconstructed_vpp_valid;
} G2026DspResult;

typedef struct
{
  arm_rfft_fast_instance_f32 fft;
  float calibrated_mv[G2026_SAMPLE_COUNT];
  float fft_input[G2026_SAMPLE_COUNT];
  float fft_output[G2026_SAMPLE_COUNT];
  float magnitude_mv[G2026_SAMPLE_COUNT / 2U + 1U];
  bool initialized;
} G2026DspContext;

G2026DspStatus G2026Dsp_Init(G2026DspContext *context);
G2026DspStatus G2026Dsp_Analyze(G2026DspContext *context,
                               const int16_t *samples,
                               uint16_t sample_count,
                               uint32_t sample_rate_hz,
                               bool clipped,
                               bool overflow,
                               const G2026DspConfig *config,
                               G2026DspResult *result);

#endif
