#include "fpga_mock_adapter.h"

#include <math.h>
#include <string.h>

#include "g2026_mock_profile.h"

#define FPGA_MOCK_PI 3.14159265358979323846F
#define FPGA_MOCK_MIN_CODE (-8192)
#define FPGA_MOCK_MAX_CODE 8191

static uint32_t AbsoluteRateError(uint32_t rate_hz)
{
  return rate_hz > G2026_NOMINAL_SAMPLE_RATE_HZ
             ? rate_hz - G2026_NOMINAL_SAMPLE_RATE_HZ
             : G2026_NOMINAL_SAMPLE_RATE_HZ - rate_hz;
}

static bool IsSignalConfigValid(const FpgaMockSignalConfig *config)
{
  if ((config == NULL) || (config->component_count == 0U) ||
      (config->component_count > FPGA_MOCK_MAX_COMPONENTS) ||
      (AbsoluteRateError(config->actual_sample_rate_hz) > 5000U) ||
      (AbsoluteRateError(config->reported_sample_rate_hz) > 5000U) ||
      !isfinite(config->millivolts_per_code) ||
      !isfinite(config->gain_error_ppm) ||
      !isfinite(config->dc_offset_mv) ||
      !isfinite(config->noise_peak_mv) ||
      (config->millivolts_per_code <= 0.0F) ||
      (config->noise_peak_mv < 0.0F))
  {
    return false;
  }

  for (uint8_t i = 0U; i < config->component_count; ++i)
  {
    const FpgaMockComponent *component = &config->components[i];
    const float snapped_frequency_hz =
        roundf(component->frequency_hz / 500.0F) * 500.0F;
    if (!isfinite(component->frequency_hz) ||
        !isfinite(component->peak_mv) ||
        !isfinite(component->phase_deg) ||
        (component->frequency_hz < 10000.0F) ||
        (component->frequency_hz > 500000.0F) ||
        (component->frequency_hz >=
         0.5F * (float)config->actual_sample_rate_hz) ||
        (component->peak_mv < 0.0F) ||
        (fabsf(component->frequency_hz -
               snapped_frequency_hz) > 0.01F) ||
        ((i > 0U) &&
         (component->frequency_hz <=
          config->components[i - 1U].frequency_hz)))
    {
      return false;
    }
    if (i > 0U)
    {
      const long harmonic_order =
          lroundf(component->frequency_hz /
                  config->components[0].frequency_hz);
      if ((harmonic_order < 2L) ||
          (fabsf(component->frequency_hz -
                 (float)harmonic_order *
                     config->components[0].frequency_hz) > 0.01F))
      {
        return false;
      }
    }
  }
  return true;
}

static void GenerateWindow(FpgaMockAdapter *adapter)
{
  uint32_t noise_state = adapter->signal.noise_seed;
  const float gain =
      1.0F + adapter->signal.gain_error_ppm * 1.0e-6F;
  adapter->window.clipped = false;

  for (uint32_t n = 0U; n < G2026_SAMPLE_COUNT; ++n)
  {
    float value_mv = 0.0F;
    const float time_s =
        (float)n /
        (float)adapter->signal.actual_sample_rate_hz;

    for (uint8_t i = 0U; i < adapter->signal.component_count; ++i)
    {
      const FpgaMockComponent *component =
          &adapter->signal.components[i];
      const float phase_rad =
          component->phase_deg * FPGA_MOCK_PI / 180.0F;
      value_mv +=
          component->peak_mv *
          cosf(2.0F * FPGA_MOCK_PI * component->frequency_hz *
                   time_s +
               phase_rad);
    }

    noise_state =
        noise_state * 1664525U + 1013904223U;
    const float normalized_noise =
        (float)((noise_state >> 8U) & 0xFFFFU) /
            32767.5F -
        1.0F;
    value_mv =
        gain * value_mv + adapter->signal.dc_offset_mv +
        adapter->signal.noise_peak_mv * normalized_noise;

    long code =
        lroundf(value_mv / adapter->signal.millivolts_per_code);
    if (code > FPGA_MOCK_MAX_CODE)
    {
      code = FPGA_MOCK_MAX_CODE;
      adapter->window.clipped = true;
    }
    else if (code < FPGA_MOCK_MIN_CODE)
    {
      code = FPGA_MOCK_MIN_CODE;
      adapter->window.clipped = true;
    }
    adapter->window.samples[n] = (int16_t)code;
  }
}

static FpgaSourceStatus Mock_Start(void *context, uint8_t channel,
                                   uint32_t now_ms)
{
  FpgaMockAdapter *adapter = (FpgaMockAdapter *)context;
  (void)now_ms;

  if ((adapter == NULL) || !adapter->configured || (channel > 1U))
  {
    return FPGA_SOURCE_INVALID_ARGUMENT;
  }
  if (adapter->active)
  {
    return FPGA_SOURCE_BUSY;
  }

  memset(&adapter->window, 0, sizeof(adapter->window));
  adapter->window.acquisition_id = adapter->next_acquisition_id++;
  adapter->window.sample_rate_hz =
      adapter->signal.reported_sample_rate_hz;
  adapter->window.filter_version = 0x00000002U;
  adapter->window.sample_count = G2026_SAMPLE_COUNT;
  adapter->window.channel = channel;
  adapter->window.effective_bits = G2026_MOCK_EFFECTIVE_BITS;

  GenerateWindow(adapter);

  adapter->status = FPGA_SOURCE_OK;
  adapter->active = true;
  adapter->done = false;
  return FPGA_SOURCE_OK;
}

static void Mock_Service(void *context, uint32_t now_ms)
{
  FpgaMockAdapter *adapter = (FpgaMockAdapter *)context;
  (void)now_ms;

  if ((adapter != NULL) && adapter->active)
  {
    adapter->active = false;
    adapter->done = true;
  }
}

static bool Mock_IsDone(const void *context)
{
  const FpgaMockAdapter *adapter = (const FpgaMockAdapter *)context;
  return (adapter != NULL) && adapter->done;
}

static FpgaSourceStatus Mock_GetWindow(void *context,
                                       FpgaSampleWindow *window)
{
  FpgaMockAdapter *adapter = (FpgaMockAdapter *)context;

  if ((adapter == NULL) || (window == NULL))
  {
    return FPGA_SOURCE_INVALID_ARGUMENT;
  }
  if (!adapter->done)
  {
    return FPGA_SOURCE_BUSY;
  }

  *window = adapter->window;
  adapter->done = false;
  return adapter->status;
}

static void Mock_Abort(void *context)
{
  FpgaMockAdapter *adapter = (FpgaMockAdapter *)context;
  if (adapter != NULL)
  {
    adapter->active = false;
    adapter->done = false;
    adapter->status = FPGA_SOURCE_ABORTED;
  }
}

void FpgaMockAdapter_Init(FpgaMockAdapter *adapter)
{
  if (adapter != NULL)
  {
    memset(adapter, 0, sizeof(*adapter));
    adapter->next_acquisition_id = 1U;
    adapter->status = FPGA_SOURCE_OK;
    const FpgaMockSignalConfig signal =
        FpgaMockAdapter_DefaultSignal();
    adapter->configured =
        FpgaMockAdapter_Configure(adapter, &signal);
  }
}

bool FpgaMockAdapter_Configure(FpgaMockAdapter *adapter,
                              const FpgaMockSignalConfig *config)
{
  if ((adapter == NULL) || adapter->active ||
      !IsSignalConfigValid(config))
  {
    return false;
  }
  adapter->signal = *config;
  adapter->configured = true;
  return true;
}

FpgaMockSignalConfig FpgaMockAdapter_DefaultSignal(void)
{
  const FpgaMockSignalConfig signal = {
      .components = {
          {
              .frequency_hz = G2026_MOCK_C1_FREQUENCY_HZ,
              .peak_mv = G2026_MOCK_C1_PEAK_MV,
              .phase_deg = G2026_MOCK_C1_PHASE_DEG,
          },
          {
              .frequency_hz = G2026_MOCK_C2_FREQUENCY_HZ,
              .peak_mv = G2026_MOCK_C2_PEAK_MV,
              .phase_deg = G2026_MOCK_C2_PHASE_DEG,
          },
          {
              .frequency_hz = G2026_MOCK_C3_FREQUENCY_HZ,
              .peak_mv = G2026_MOCK_C3_PEAK_MV,
              .phase_deg = G2026_MOCK_C3_PHASE_DEG,
          },
      },
      .actual_sample_rate_hz =
          G2026_MOCK_ACTUAL_SAMPLE_RATE_HZ,
      .reported_sample_rate_hz =
          G2026_MOCK_REPORTED_SAMPLE_RATE_HZ,
      .noise_seed = G2026_MOCK_NOISE_SEED,
      .millivolts_per_code = G2026_MOCK_MILLIVOLTS_PER_CODE,
      .gain_error_ppm = G2026_MOCK_GAIN_ERROR_PPM,
      .dc_offset_mv = G2026_MOCK_DC_OFFSET_MV,
      .noise_peak_mv = G2026_MOCK_NOISE_PEAK_MV,
      .component_count = G2026_MOCK_COMPONENT_COUNT,
  };
  return signal;
}

FpgaDataSource FpgaMockAdapter_MakeSource(FpgaMockAdapter *adapter)
{
  const FpgaDataSource source = {
      .context = adapter,
      .start = Mock_Start,
      .service = Mock_Service,
      .is_done = Mock_IsDone,
      .get_window = Mock_GetWindow,
      .abort = Mock_Abort,
  };
  return source;
}
