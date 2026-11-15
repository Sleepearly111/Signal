#ifndef FPGA_DATA_SOURCE_H
#define FPGA_DATA_SOURCE_H

#include <stdbool.h>
#include <stdint.h>

#include "g2026_config.h"

typedef enum
{
  FPGA_SOURCE_OK = 0,
  FPGA_SOURCE_BUSY,
  FPGA_SOURCE_INVALID_ARGUMENT,
  FPGA_SOURCE_TIMEOUT,
  FPGA_SOURCE_DATA_INVALID,
  FPGA_SOURCE_ABORTED
} FpgaSourceStatus;

typedef struct
{
  uint32_t acquisition_id;
  uint32_t sample_rate_hz;
  uint32_t filter_version;
  uint16_t sample_count;
  uint8_t channel;
  uint8_t effective_bits;
  bool clipped;
  bool overflow;
  int16_t samples[G2026_SAMPLE_COUNT];
} FpgaSampleWindow;

typedef struct
{
  void *context;
  FpgaSourceStatus (*start)(void *context, uint8_t channel,
                            uint32_t now_ms);
  void (*service)(void *context, uint32_t now_ms);
  bool (*is_done)(const void *context);
  FpgaSourceStatus (*get_window)(void *context, FpgaSampleWindow *window);
  void (*abort)(void *context);
} FpgaDataSource;

#endif
