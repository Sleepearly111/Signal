#include "g2026_calibration.h"

#include <math.h>
#include <string.h>

static bool KeysEqual(const G2026CalibrationKey *left,
                      const G2026CalibrationKey *right)
{
  return (left->adc_model == right->adc_model) &&
         (left->channel_id == right->channel_id) &&
         (left->frontend_range_id == right->frontend_range_id) &&
         (left->filter_version == right->filter_version);
}

static bool KeyIsValid(const G2026CalibrationKey *key)
{
  return (key != NULL) &&
         ((key->adc_model == 1U) || (key->adc_model == 2U)) &&
         (key->channel_id < 2U) &&
         (key->frontend_range_id <= 4U);
}

static bool NumericValuesAreValid(
    const G2026CalibrationRecord *record)
{
  return isfinite(record->input_mv_per_code) &&
         (record->input_mv_per_code > 0.0F) &&
         isfinite(record->zero_code) &&
         isfinite(record->sample_clock_ppm);
}

void G2026Calibration_Init(G2026CalibrationStore *store)
{
  if (store != NULL)
  {
    /*
     * An empty store is intentional. There is no nominal-gain fallback:
     * every usable entry must come from a measured calibration record.
     */
    memset(store, 0, sizeof(*store));
  }
}

G2026CalibrationStatus G2026Calibration_Put(
    G2026CalibrationStore *store,
    const G2026CalibrationRecord *record)
{
  size_t free_index = G2026_CALIBRATION_MAX_RECORDS;

  if ((store == NULL) || (record == NULL) ||
      !KeyIsValid(&record->key))
  {
    return G2026_CALIBRATION_INVALID_ARGUMENT;
  }
  if (record->valid && !NumericValuesAreValid(record))
  {
    return G2026_CALIBRATION_RECORD_INVALID;
  }

  for (size_t i = 0U; i < G2026_CALIBRATION_MAX_RECORDS; ++i)
  {
    if (store->slots[i].occupied)
    {
      if (KeysEqual(&store->slots[i].record.key, &record->key))
      {
        store->slots[i].record = *record;
        return G2026_CALIBRATION_OK;
      }
    }
    else if (free_index == G2026_CALIBRATION_MAX_RECORDS)
    {
      free_index = i;
    }
  }

  if (free_index == G2026_CALIBRATION_MAX_RECORDS)
  {
    return G2026_CALIBRATION_STORE_FULL;
  }
  store->slots[free_index].record = *record;
  store->slots[free_index].occupied = true;
  return G2026_CALIBRATION_OK;
}

G2026CalibrationStatus G2026Calibration_Select(
    const G2026CalibrationStore *store,
    const G2026CalibrationKey *key,
    G2026CalibrationRecord *record)
{
  if ((store == NULL) || (key == NULL) || (record == NULL) ||
      !KeyIsValid(key))
  {
    return G2026_CALIBRATION_INVALID_ARGUMENT;
  }

  for (size_t i = 0U; i < G2026_CALIBRATION_MAX_RECORDS; ++i)
  {
    const G2026CalibrationSlot *slot = &store->slots[i];
    if (slot->occupied && KeysEqual(&slot->record.key, key))
    {
      if (!slot->record.valid ||
          !NumericValuesAreValid(&slot->record))
      {
        return G2026_CALIBRATION_RECORD_INVALID;
      }
      *record = slot->record;
      return G2026_CALIBRATION_OK;
    }
  }
  return G2026_CALIBRATION_NOT_FOUND;
}

bool G2026Calibration_CorrectSampleRate(
    const G2026CalibrationRecord *record,
    uint32_t reported_sample_rate_hz,
    float *corrected_sample_rate_hz)
{
  float corrected;

  if ((record == NULL) || (corrected_sample_rate_hz == NULL) ||
      !record->valid || !NumericValuesAreValid(record) ||
      (reported_sample_rate_hz == 0U))
  {
    return false;
  }

  corrected = (float)reported_sample_rate_hz *
              (1.0F + record->sample_clock_ppm * 1.0e-6F);
  if (!isfinite(corrected) || (corrected <= 0.0F))
  {
    return false;
  }
  *corrected_sample_rate_hz = corrected;
  return true;
}

typedef struct
{
  float x;
  float y;
} G2026CalibrationPoint;

static float InterpolateClamped(const G2026CalibrationPoint *points,
                                size_t count,
                                float x)
{
  if (x <= points[0].x)
  {
    return points[0].y;
  }
  for (size_t i = 1U; i < count; ++i)
  {
    if (x <= points[i].x)
    {
      const float fraction =
          (x - points[i - 1U].x) /
          (points[i].x - points[i - 1U].x);
      return points[i - 1U].y +
             fraction * (points[i].y - points[i - 1U].y);
    }
  }
  return points[count - 1U].y;
}

static float InterpolateWithEndExtrapolation(
    const G2026CalibrationPoint *points,
    size_t count,
    float x)
{
  if (x <= points[0].x)
  {
    return points[0].y;
  }
  for (size_t i = 1U; i < count; ++i)
  {
    if (x <= points[i].x)
    {
      const float fraction =
          (x - points[i - 1U].x) /
          (points[i].x - points[i - 1U].x);
      return points[i - 1U].y +
             fraction * (points[i].y - points[i - 1U].y);
    }
  }

  const size_t last = count - 1U;
  const float fraction =
      (x - points[last - 1U].x) /
      (points[last].x - points[last - 1U].x);
  return points[last - 1U].y +
         fraction * (points[last].y - points[last - 1U].y);
}

bool G2026Calibration_CorrectDirectVpp(float frequency_hz,
                                       float measured_vpp_mv,
                                       float *corrected_vpp_mv)
{
  /*
   * Mean ratios across the complete 10..250 mVpp sweep, normalized to the
   * corresponding 10 kHz value.  Pure-sine points fit this table; composite
   * waves are kept as the independent validation set.
   */
  static const G2026CalibrationPoint frequency_response[] = {
      {10000.0F, 1.0000000F},
      {15000.0F, 1.0001886F},
      {20000.0F, 1.0002881F},
      {30000.0F, 1.0001276F},
      {40000.0F, 0.9993777F},
      {50000.0F, 0.9990220F},
      {75000.0F, 0.9995130F},
      {100000.0F, 0.9998942F},
      {125000.0F, 0.9998745F},
      {150000.0F, 1.0002211F},
      {175000.0F, 1.0005801F},
      {200000.0F, 1.0006405F},
      {225000.0F, 1.0009797F},
      {250000.0F, 0.9996824F},
      {300000.0F, 0.9990931F},
      {350000.0F, 0.9988767F},
      {400000.0F, 0.9995145F},
      {450000.0F, 1.0009926F},
      {500000.0F, 0.9971567F},
  };
  /* Mean inverse transfer after frequency normalization. */
  static const G2026CalibrationPoint amplitude_inverse[] = {
      {0.0F, 0.0F},
      {10.0995458F, 10.0F},
      {15.1380182F, 15.0F},
      {20.1368777F, 20.0F},
      {24.8532530F, 25.0F},
      {29.5829139F, 30.0F},
      {39.4571595F, 40.0F},
      {49.4934442F, 50.0F},
      {59.4516612F, 60.0F},
      {74.5952046F, 75.0F},
      {89.7343834F, 90.0F},
      {99.7402226F, 100.0F},
      {124.9781340F, 125.0F},
      {149.8271605F, 150.0F},
      {175.1718387F, 175.0F},
      {200.1359824F, 200.0F},
      {225.4531342F, 225.0F},
      {250.7196259F, 250.0F},
  };
  float response;
  float reference_vpp;
  float corrected;

  if ((corrected_vpp_mv == NULL) || !isfinite(frequency_hz) ||
      !isfinite(measured_vpp_mv) || (frequency_hz <= 0.0F) ||
      (measured_vpp_mv < 0.0F))
  {
    return false;
  }

  response = InterpolateClamped(
      frequency_response,
      sizeof(frequency_response) / sizeof(frequency_response[0]),
      frequency_hz);
  if (!isfinite(response) || (response <= 0.0F))
  {
    return false;
  }
  reference_vpp = measured_vpp_mv / response;
  corrected = InterpolateWithEndExtrapolation(
      amplitude_inverse,
      sizeof(amplitude_inverse) / sizeof(amplitude_inverse[0]),
      reference_vpp);
  if (!isfinite(corrected) || (corrected < 0.0F))
  {
    return false;
  }
  *corrected_vpp_mv = corrected;
  return true;
}
