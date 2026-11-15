#include <assert.h>
#include <stdio.h>

#include "g2026_sampling_quality.h"

int main(void)
{
  assert(G2026SamplingQuality_IsWindowValid(
      2000000U, 4096U, 500000U, 500U));
  assert(G2026SamplingQuality_IsWindowValid(
      2048000U, 4096U, 500000U, 500U));
  assert(!G2026SamplingQuality_IsWindowValid(
      2048001U, 4096U, 500000U, 500U));
  assert(!G2026SamplingQuality_IsWindowValid(
      1000000U, 4096U, 500000U, 500U));
  assert(G2026SamplingQuality_IsWindowValid(
      1000001U, 4096U, 500000U, 500U));
  assert(!G2026SamplingQuality_IsWindowValid(
      2000000U, 0U, 500000U, 500U));
  assert(!G2026SamplingQuality_IsWindowValid(
      2000000U, 4096U, 0U, 500U));
  assert(!G2026SamplingQuality_IsWindowValid(
      2000000U, 4096U, 500000U, 0U));

  puts("g2026 sampling quality tests passed");
  return 0;
}
