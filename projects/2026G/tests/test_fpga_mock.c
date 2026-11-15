#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "fpga_mock_adapter.h"

int main(void)
{
  FpgaMockAdapter adapter;
  FpgaDataSource source;
  FpgaSampleWindow window;
  int16_t minimum = INT16_MAX;
  int16_t maximum = INT16_MIN;
  double square_sum = 0.0;

  FpgaMockAdapter_Init(&adapter);
  source = FpgaMockAdapter_MakeSource(&adapter);

  assert(source.start(source.context, 0U, 10U) == FPGA_SOURCE_OK);
  assert(!source.is_done(source.context));
  assert(source.start(source.context, 0U, 10U) == FPGA_SOURCE_BUSY);
  source.service(source.context, 11U);
  assert(source.is_done(source.context));
  assert(source.get_window(source.context, &window) == FPGA_SOURCE_OK);

  assert(window.acquisition_id == 1U);
  assert(window.sample_rate_hz == 2000000U);
  assert(window.sample_count == 4096U);
  assert(window.effective_bits == 14U);
  assert(!window.clipped);
  assert(!window.overflow);

  for (uint32_t i = 0U; i < window.sample_count; ++i)
  {
    if (window.samples[i] < minimum)
    {
      minimum = window.samples[i];
    }
    if (window.samples[i] > maximum)
    {
      maximum = window.samples[i];
    }
    square_sum +=
        (double)window.samples[i] * (double)window.samples[i];
  }
  assert(minimum < -4500);
  assert(maximum > 5800);
  assert(window.samples[0] > 6000);
  assert(window.samples[0] < 6200);
  const double rms_codes =
      sqrt(square_sum / (double)window.sample_count);
  assert(rms_codes > 2840.0);
  assert(rms_codes < 2900.0);

  FpgaMockSignalConfig invalid = FpgaMockAdapter_DefaultSignal();
  invalid.actual_sample_rate_hz = 2048000U;
  assert(!FpgaMockAdapter_Configure(&adapter, &invalid));

  source.abort(source.context);
  source.abort(source.context);
  assert(!source.is_done(source.context));
  assert(source.start(source.context, 2U, 20U) ==
         FPGA_SOURCE_INVALID_ARGUMENT);

  puts("g2026 fpga mock tests passed");
  return 0;
}
