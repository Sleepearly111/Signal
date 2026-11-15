#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "g2026_dual_gain.h"

static void TestPerComponentGain(void)
{
  G2026DspResult input;
  G2026DspResult output;
  G2026DualGainResult gain;

  memset(&input, 0, sizeof(input));
  memset(&output, 0, sizeof(output));
  input.component_count = 3U;
  output.component_count = 3U;
  input.spectral_rms_mv = 80.0F;
  output.spectral_rms_mv = 320.0F;

  for (uint8_t i = 0U; i < 3U; ++i)
  {
    const uint8_t order = (uint8_t)(1U + 2U * i);
    input.components[i].frequency_hz = 50000.0F * order;
    input.components[i].peak_mv = 100.0F / (float)(i + 1U);
    input.components[i].harmonic_order = order;
    input.components[i].harmonic_valid = true;

    output.components[2U - i].frequency_hz =
        input.components[i].frequency_hz + 100.0F;
    output.components[2U - i].peak_mv =
        input.components[i].peak_mv * (4.0F - 0.5F * i);
    output.components[2U - i].harmonic_order = order;
    output.components[2U - i].harmonic_valid = true;
  }

  assert(G2026DualGain_Calculate(
             &input, &output, 1.0F, 200.0F, &gain) ==
         G2026_DUAL_GAIN_OK);
  assert(gain.component_count == 3U);
  assert(fabsf(gain.rms_gain - 4.0F) < 0.001F);
  assert(fabsf(gain.components[0].gain - 4.0F) < 0.001F);
  assert(fabsf(gain.components[1].gain - 3.5F) < 0.001F);
  assert(fabsf(gain.components[2].gain - 3.0F) < 0.001F);
}

static void TestRejectsUnusableInput(void)
{
  G2026DspResult input;
  G2026DspResult output;
  G2026DualGainResult gain;

  memset(&input, 0, sizeof(input));
  memset(&output, 0, sizeof(output));
  input.component_count = 1U;
  input.components[0].frequency_hz = 10000.0F;
  input.components[0].peak_mv = 0.1F;
  output.component_count = 1U;
  output.components[0].frequency_hz = 10000.0F;
  output.components[0].peak_mv = 10.0F;

  assert(G2026DualGain_Calculate(
             &input, &output, 1.0F, 200.0F, &gain) ==
         G2026_DUAL_GAIN_INPUT_TOO_SMALL);
}

int main(void)
{
  TestPerComponentGain();
  TestRejectsUnusableInput();
  puts("g2026 dual gain tests passed");
  return 0;
}
