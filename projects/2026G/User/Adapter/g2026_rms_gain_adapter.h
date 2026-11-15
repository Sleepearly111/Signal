#ifndef G2026_RMS_GAIN_ADAPTER_H
#define G2026_RMS_GAIN_ADAPTER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
  G2026_RMS_ADC_READY = 0,
  G2026_RMS_ADC_BUSY,
  G2026_RMS_ADC_ERROR
} G2026_RmsAdcStatus;

/*
 * Optional board adapter contract. It is not part of the AD9248 dual-channel
 * THS4001 measurement path.
 *
 * set_mux():
 *   enable=false -> CD4051 INH high (all analogue paths disconnected)
 *   enable=true  -> set C/B/A from channel[2:0], then INH low
 *
 * read_rms_mv():
 *   return one calibrated ADS8688 reading of the AD637 DC output in mV.
 *   Use one dedicated MANUAL channel; do not mix it with an AUTO sequence.
 */
typedef struct
{
  void *context;
  bool (*set_mux)(void *context, uint8_t channel, bool enable);
  G2026_RmsAdcStatus (*read_rms_mv)(void *context, float *millivolts);
} G2026_RmsGainAdapter;

#endif
