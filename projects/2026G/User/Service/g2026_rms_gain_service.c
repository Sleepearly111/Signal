#include "g2026_rms_gain_service.h"

#include <float.h>
#include <string.h>

static bool reached(uint32_t now_ms, uint32_t target_ms)
{
  return (int32_t)(now_ms - target_ms) >= 0;
}

static void reset_samples(G2026_RmsGainService *service)
{
  service->sum_mv = 0.0f;
  service->minimum_mv = FLT_MAX;
  service->maximum_mv = -FLT_MAX;
  service->sample_count = 0U;
}

static float filtered_mean(const G2026_RmsGainService *service)
{
  if (service->sample_count > 2U)
  {
    return (service->sum_mv - service->minimum_mv - service->maximum_mv) /
           (float)(service->sample_count - 2U);
  }
  return service->sum_mv / (float)service->sample_count;
}

static void finish(G2026_RmsGainService *service,
                   G2026_RmsGainResultCode code)
{
  (void)service->adapter.set_mux(service->adapter.context, 0U, false);
  service->result.code = code;
  service->state = (code == G2026_RMS_GAIN_RESULT_OK)
                       ? G2026_RMS_GAIN_COMPLETE
                       : G2026_RMS_GAIN_FAULT;
}

static bool select_path(G2026_RmsGainService *service,
                        uint8_t channel,
                        uint32_t now_ms,
                        G2026_RmsGainState settle_state)
{
  if (!service->adapter.set_mux(service->adapter.context, 0U, false) ||
      !service->adapter.set_mux(service->adapter.context, channel, true))
  {
    finish(service, G2026_RMS_GAIN_RESULT_ADAPTER_ERROR);
    return false;
  }
  service->next_action_ms = now_ms + service->config.settle_ms;
  service->state = settle_state;
  return true;
}

bool G2026_RmsGainService_Init(G2026_RmsGainService *service,
                               const G2026_RmsGainAdapter *adapter,
                               const G2026_RmsGainConfig *config)
{
  if ((service == NULL) || (adapter == NULL) || (config == NULL) ||
      (adapter->set_mux == NULL) || (adapter->read_rms_mv == NULL) ||
      (config->input_mux_channel > 7U) ||
      (config->output_mux_channel > 7U) ||
      (config->input_mux_channel == config->output_mux_channel) ||
      (config->samples_per_path == 0U) ||
      (config->total_timeout_ms == 0U) ||
      (config->minimum_input_rms_mv <= 0.0f) ||
      (config->minimum_allowed_gain <= 0.0f) ||
      (config->maximum_allowed_gain < config->minimum_allowed_gain))
  {
    return false;
  }

  memset(service, 0, sizeof(*service));
  service->adapter = *adapter;
  service->config = *config;
  service->state = G2026_RMS_GAIN_IDLE;
  (void)service->adapter.set_mux(service->adapter.context, 0U, false);
  return true;
}

bool G2026_RmsGainService_Start(G2026_RmsGainService *service,
                                uint32_t now_ms)
{
  if ((service == NULL) ||
      ((service->state != G2026_RMS_GAIN_IDLE) &&
       (service->state != G2026_RMS_GAIN_COMPLETE) &&
       (service->state != G2026_RMS_GAIN_FAULT)))
  {
    return false;
  }

  memset(&service->result, 0, sizeof(service->result));
  reset_samples(service);
  service->deadline_ms = now_ms + service->config.total_timeout_ms;
  return select_path(service, service->config.input_mux_channel, now_ms,
                     G2026_RMS_GAIN_SETTLE_INPUT);
}

static void acquire(G2026_RmsGainService *service,
                    uint32_t now_ms,
                    bool input_path)
{
  float millivolts = 0.0f;
  G2026_RmsAdcStatus status =
      service->adapter.read_rms_mv(service->adapter.context, &millivolts);
  if (status == G2026_RMS_ADC_BUSY)
  {
    return;
  }
  if ((status != G2026_RMS_ADC_READY) || (millivolts < 0.0f))
  {
    finish(service, G2026_RMS_GAIN_RESULT_ADAPTER_ERROR);
    return;
  }

  service->sum_mv += millivolts;
  if (millivolts < service->minimum_mv)
  {
    service->minimum_mv = millivolts;
  }
  if (millivolts > service->maximum_mv)
  {
    service->maximum_mv = millivolts;
  }
  service->sample_count++;
  service->next_action_ms = now_ms + service->config.sample_interval_ms;

  if (service->sample_count < service->config.samples_per_path)
  {
    return;
  }

  if (input_path)
  {
    service->result.input_detector_mv = filtered_mean(service);
    if (service->result.input_detector_mv <
        service->config.minimum_input_rms_mv)
    {
      finish(service, G2026_RMS_GAIN_RESULT_INVALID_INPUT);
      return;
    }
    reset_samples(service);
    (void)select_path(service, service->config.output_mux_channel, now_ms,
                      G2026_RMS_GAIN_SETTLE_OUTPUT);
    return;
  }

  service->result.output_detector_mv = filtered_mean(service);
  service->result.measured_gain =
      service->result.output_detector_mv /
      service->result.input_detector_mv;
  if ((service->result.measured_gain <
       service->config.minimum_allowed_gain) ||
      (service->result.measured_gain >
       service->config.maximum_allowed_gain))
  {
    finish(service, G2026_RMS_GAIN_RESULT_OUT_OF_RANGE);
  }
  else
  {
    finish(service, G2026_RMS_GAIN_RESULT_OK);
  }
}

void G2026_RmsGainService_Process(G2026_RmsGainService *service,
                                  uint32_t now_ms)
{
  if (service == NULL)
  {
    return;
  }
  if ((service->state != G2026_RMS_GAIN_IDLE) &&
      (service->state != G2026_RMS_GAIN_COMPLETE) &&
      (service->state != G2026_RMS_GAIN_FAULT) &&
      reached(now_ms, service->deadline_ms))
  {
    finish(service, G2026_RMS_GAIN_RESULT_TIMEOUT);
    return;
  }

  switch (service->state)
  {
    case G2026_RMS_GAIN_SETTLE_INPUT:
      if (reached(now_ms, service->next_action_ms))
      {
        reset_samples(service);
        service->state = G2026_RMS_GAIN_SAMPLE_INPUT;
      }
      break;
    case G2026_RMS_GAIN_SAMPLE_INPUT:
      if (reached(now_ms, service->next_action_ms))
      {
        acquire(service, now_ms, true);
      }
      break;
    case G2026_RMS_GAIN_SETTLE_OUTPUT:
      if (reached(now_ms, service->next_action_ms))
      {
        reset_samples(service);
        service->state = G2026_RMS_GAIN_SAMPLE_OUTPUT;
      }
      break;
    case G2026_RMS_GAIN_SAMPLE_OUTPUT:
      if (reached(now_ms, service->next_action_ms))
      {
        acquire(service, now_ms, false);
      }
      break;
    default:
      break;
  }
}

void G2026_RmsGainService_Abort(G2026_RmsGainService *service)
{
  if (service != NULL)
  {
    (void)service->adapter.set_mux(service->adapter.context, 0U, false);
    service->state = G2026_RMS_GAIN_IDLE;
    service->result.code = G2026_RMS_GAIN_RESULT_NONE;
  }
}

bool G2026_RmsGainService_TakeResult(G2026_RmsGainService *service,
                                    G2026_RmsGainResult *result)
{
  if ((service == NULL) || (result == NULL) ||
      ((service->state != G2026_RMS_GAIN_COMPLETE) &&
       (service->state != G2026_RMS_GAIN_FAULT)))
  {
    return false;
  }
  *result = service->result;
  service->state = G2026_RMS_GAIN_IDLE;
  service->result.code = G2026_RMS_GAIN_RESULT_NONE;
  return true;
}
