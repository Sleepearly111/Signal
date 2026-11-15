#ifndef G2026_RMS_GAIN_SERVICE_H
#define G2026_RMS_GAIN_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "g2026_rms_gain_adapter.h"

typedef enum
{
  G2026_RMS_GAIN_IDLE = 0,
  G2026_RMS_GAIN_SETTLE_INPUT,
  G2026_RMS_GAIN_SAMPLE_INPUT,
  G2026_RMS_GAIN_SETTLE_OUTPUT,
  G2026_RMS_GAIN_SAMPLE_OUTPUT,
  G2026_RMS_GAIN_COMPLETE,
  G2026_RMS_GAIN_FAULT
} G2026_RmsGainState;

typedef enum
{
  G2026_RMS_GAIN_RESULT_NONE = 0,
  G2026_RMS_GAIN_RESULT_OK,
  G2026_RMS_GAIN_RESULT_INVALID_INPUT,
  G2026_RMS_GAIN_RESULT_OUT_OF_RANGE,
  G2026_RMS_GAIN_RESULT_ADAPTER_ERROR,
  G2026_RMS_GAIN_RESULT_TIMEOUT
} G2026_RmsGainResultCode;

typedef struct
{
  uint8_t input_mux_channel;
  uint8_t output_mux_channel;
  uint16_t samples_per_path;
  uint32_t settle_ms;
  uint32_t sample_interval_ms;
  uint32_t total_timeout_ms;
  float minimum_input_rms_mv;
  float minimum_allowed_gain;
  float maximum_allowed_gain;
} G2026_RmsGainConfig;

typedef struct
{
  float input_detector_mv;
  float output_detector_mv;
  float measured_gain;
  G2026_RmsGainResultCode code;
} G2026_RmsGainResult;

typedef struct
{
  G2026_RmsGainAdapter adapter;
  G2026_RmsGainConfig config;
  G2026_RmsGainState state;
  G2026_RmsGainResult result;
  uint32_t deadline_ms;
  uint32_t next_action_ms;
  float sum_mv;
  float minimum_mv;
  float maximum_mv;
  uint16_t sample_count;
} G2026_RmsGainService;

bool G2026_RmsGainService_Init(G2026_RmsGainService *service,
                               const G2026_RmsGainAdapter *adapter,
                               const G2026_RmsGainConfig *config);
bool G2026_RmsGainService_Start(G2026_RmsGainService *service,
                                uint32_t now_ms);
void G2026_RmsGainService_Process(G2026_RmsGainService *service,
                                  uint32_t now_ms);
void G2026_RmsGainService_Abort(G2026_RmsGainService *service);
bool G2026_RmsGainService_TakeResult(G2026_RmsGainService *service,
                                    G2026_RmsGainResult *result);

#endif
