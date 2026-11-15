#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "g2026_calibration.h"

int main(void)
{
  G2026CalibrationStore store;
  G2026CalibrationKey key = {
      .adc_model = 2U,
      .channel_id = 0U,
      .frontend_range_id = 2U,
      .filter_version = 0x10203040U,
  };
  G2026CalibrationRecord record = {
      .key = {
          .adc_model = 2U,
          .channel_id = 0U,
          .frontend_range_id = 2U,
          .filter_version = 0x10203040U,
      },
      .input_mv_per_code = 0.012345F,
      .zero_code = 1.75F,
      .sample_clock_ppm = -125.0F,
      .calibration_id = 77U,
      .valid = false,
  };
  G2026CalibrationRecord selected;
  float corrected_rate;
  float corrected_vpp;

  G2026Calibration_Init(&store);

  /* Range 0 is the explicit direct-INA path, not an unknown wildcard. */
  record.key.frontend_range_id = 0U;
  record.valid = true;
  assert(G2026Calibration_Put(&store, &record) == G2026_CALIBRATION_OK);
  assert(G2026Calibration_Select(&store, &record.key, &selected) ==
         G2026_CALIBRATION_OK);
  assert(selected.key.frontend_range_id == 0U);
  G2026Calibration_Init(&store);
  record.key = key;
  record.valid = false;
  assert(G2026Calibration_Select(&store, &key, &selected) ==
         G2026_CALIBRATION_NOT_FOUND);

  assert(G2026Calibration_Put(&store, &record) ==
         G2026_CALIBRATION_OK);
  assert(G2026Calibration_Select(&store, &key, &selected) ==
         G2026_CALIBRATION_RECORD_INVALID);

  record.valid = true;
  assert(G2026Calibration_Put(&store, &record) ==
         G2026_CALIBRATION_OK);
  assert(G2026Calibration_Select(&store, &key, &selected) ==
         G2026_CALIBRATION_OK);
  assert(selected.input_mv_per_code == record.input_mv_per_code);
  assert(selected.input_mv_per_code != 0.5F);
  assert(G2026Calibration_CorrectSampleRate(
      &selected, 2000000U, &corrected_rate));
  assert(fabsf(corrected_rate - 1999750.0F) < 0.25F);

  assert(G2026Calibration_CorrectDirectVpp(
      10000.0F, 10.0995458F, &corrected_vpp));
  assert(fabsf(corrected_vpp - 10.0F) < 0.01F);
  assert(G2026Calibration_CorrectDirectVpp(
      10000.0F, 99.7402226F, &corrected_vpp));
  assert(fabsf(corrected_vpp - 100.0F) < 0.01F);
  assert(G2026Calibration_CorrectDirectVpp(
      10000.0F, 250.7196259F, &corrected_vpp));
  assert(fabsf(corrected_vpp - 250.0F) < 0.01F);
  /* Frequency normalization must preserve the amplitude-map anchors. */
  assert(G2026Calibration_CorrectDirectVpp(
      500000.0F, 10.0708280F, &corrected_vpp));
  assert(fabsf(corrected_vpp - 10.0F) < 0.01F);
  assert(G2026Calibration_CorrectDirectVpp(
      500000.0F, 99.4566255F, &corrected_vpp));
  assert(fabsf(corrected_vpp - 100.0F) < 0.05F);
  assert(!G2026Calibration_CorrectDirectVpp(
      0.0F, 100.0F, &corrected_vpp));

  key.frontend_range_id = 3U;
  assert(G2026Calibration_Select(&store, &key, &selected) ==
         G2026_CALIBRATION_NOT_FOUND);
  record.key.frontend_range_id = 4U;
  record.key.filter_version = key.filter_version;
  record.input_mv_per_code = 0.01F;
  assert(G2026Calibration_Put(&store, &record) ==
         G2026_CALIBRATION_OK);
  key.frontend_range_id = 2U;
  key.filter_version++;
  assert(G2026Calibration_Select(&store, &key, &selected) ==
         G2026_CALIBRATION_NOT_FOUND);

  record.key.filter_version++;
  record.input_mv_per_code = 0.0F;
  assert(G2026Calibration_Put(&store, &record) ==
         G2026_CALIBRATION_RECORD_INVALID);

  puts("g2026 calibration tests passed");
  return 0;
}
