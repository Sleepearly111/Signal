#include <assert.h>
#include <stdio.h>

#include "g2026_ad8421_calibration.h"

int main(void)
{
  float gain = 0.0F;
  float scale = 0.0F;

  /* Delivery starts safely gated until measured ratios replace zeros. */
  assert(G2026_AD8421_GAIN_TABLE_VALID == 0U);
  assert(!G2026Ad8421Calibration_Lookup(
      100000.0F, &scale, &gain));
  puts("g2026 AD8421 calibration gate test passed");
  return 0;
}
