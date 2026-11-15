#ifndef FPGA_MOCK_ADAPTER_H
#define FPGA_MOCK_ADAPTER_H

#include "fpga_data_source.h"

#define FPGA_MOCK_MAX_COMPONENTS 3U

typedef struct
{
  float frequency_hz;
  float peak_mv;
  float phase_deg;
} FpgaMockComponent;

typedef struct
{
  FpgaMockComponent components[FPGA_MOCK_MAX_COMPONENTS];
  uint32_t actual_sample_rate_hz;
  uint32_t reported_sample_rate_hz;
  uint32_t noise_seed;
  float millivolts_per_code;
  float gain_error_ppm;
  float dc_offset_mv;
  float noise_peak_mv;
  uint8_t component_count;
} FpgaMockSignalConfig;

typedef struct
{
  FpgaSampleWindow window;
  FpgaMockSignalConfig signal;
  uint32_t next_acquisition_id;
  FpgaSourceStatus status;
  bool active;
  bool done;
  bool configured;
} FpgaMockAdapter;

void FpgaMockAdapter_Init(FpgaMockAdapter *adapter);
bool FpgaMockAdapter_Configure(FpgaMockAdapter *adapter,
                              const FpgaMockSignalConfig *config);
FpgaMockSignalConfig FpgaMockAdapter_DefaultSignal(void);
FpgaDataSource FpgaMockAdapter_MakeSource(FpgaMockAdapter *adapter);

#endif
