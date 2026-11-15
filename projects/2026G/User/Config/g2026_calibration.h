#ifndef G2026_CALIBRATION_H
#define G2026_CALIBRATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define G2026_CALIBRATION_MAX_RECORDS 16U

typedef struct
{
  uint8_t adc_model;
  uint8_t channel_id;
  uint8_t frontend_range_id;
  uint32_t filter_version;
} G2026CalibrationKey;

typedef struct
{
  G2026CalibrationKey key;
  /*
   * input_mv_per_code is measured at the apparatus input connector. It
   * already includes the real analogue-path gain and ADC transfer scale.
   * No code may derive it from the nominal 1x/2x/4x range identity.
   */
  float input_mv_per_code;
  float zero_code;
  float sample_clock_ppm;
  uint32_t calibration_id;
  bool valid;
} G2026CalibrationRecord;

typedef struct
{
  G2026CalibrationRecord record;
  bool occupied;
} G2026CalibrationSlot;

typedef struct
{
  G2026CalibrationSlot slots[G2026_CALIBRATION_MAX_RECORDS];
} G2026CalibrationStore;

typedef enum
{
  G2026_CALIBRATION_OK = 0,
  G2026_CALIBRATION_INVALID_ARGUMENT,
  G2026_CALIBRATION_STORE_FULL,
  G2026_CALIBRATION_NOT_FOUND,
  G2026_CALIBRATION_RECORD_INVALID
} G2026CalibrationStatus;

void G2026Calibration_Init(G2026CalibrationStore *store);
G2026CalibrationStatus G2026Calibration_Put(
    G2026CalibrationStore *store,
    const G2026CalibrationRecord *record);
G2026CalibrationStatus G2026Calibration_Select(
    const G2026CalibrationStore *store,
    const G2026CalibrationKey *key,
    G2026CalibrationRecord *record);
bool G2026Calibration_CorrectSampleRate(
    const G2026CalibrationRecord *record,
    uint32_t reported_sample_rate_hz,
    float *corrected_sample_rate_hz);

/*
 * Correct a grid-fit component Vpp measured by the direct INA path.
 * The table is valid only for the 2026-08-01 direct-generator setup,
 * 2 MSPS / 4096 samples / filter 0x20260730.  Frequency response is
 * removed first, followed by the measured monotonic amplitude transfer and
 * the conservative warmed-up piecewise uplift.  The uplift deliberately
 * excludes non-repeatable readings observed after moving the cable.
 */
bool G2026Calibration_CorrectDirectVpp(float frequency_hz,
                                       float measured_vpp_mv,
                                       float *corrected_vpp_mv);

#endif
