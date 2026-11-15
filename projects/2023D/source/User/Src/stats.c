#include "stats.h"
#include "app_config.h"
#include <stdlib.h>
#include <string.h>

/* 12bit ADC 值用直方图求百分位, O(n), 无需排序 */
float Stats_EnvPercentile(const uint16_t *x, int n, float p)
{
  static uint16_t hist[4096];
  memset(hist, 0, sizeof hist);
  for (int i = 0; i < n; i++) hist[x[i] & 0x0FFF]++;
  int target = (int)(p * n), acc = 0;
  if (target < 1) target = 1;
  for (int v = 0; v < 4096; v++) {
    acc += hist[v];
    if (acc >= target) return (float)v;
  }
  return 4095.0f;
}

static int cmp_f(const void *a, const void *b)
{
  float d = *(const float *)a - *(const float *)b;
  return (d > 0) - (d < 0);
}

float Stats_FloatPercentile(const float *x, int n, float p)
{
  static float tmp[FREQ_MAX_SAMPLES];
  if (n > FREQ_MAX_SAMPLES) n = FREQ_MAX_SAMPLES;
  memcpy(tmp, x, n * sizeof(float));
  qsort(tmp, n, sizeof(float), cmp_f);
  int i = (int)(p * (n - 1) + 0.5f);
  return tmp[i];
}

float Stats_MidFractionU16(const uint16_t *x, int n, float lo, float hi)
{
  float a = lo + 0.3f * (hi - lo), b = lo + 0.7f * (hi - lo);
  int c = 0;
  for (int i = 0; i < n; i++)
    if (x[i] > a && x[i] < b) c++;
  return (float)c / n;
}

float Stats_MidFractionF(const float *x, int n, float lo, float hi)
{
  float a = lo + 0.3f * (hi - lo), b = lo + 0.7f * (hi - lo);
  int c = 0;
  for (int i = 0; i < n; i++)
    if (x[i] > a && x[i] < b) c++;
  return (float)c / n;
}
