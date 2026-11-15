#ifndef STATS_H
#define STATS_H

#include <stdint.h>

float Stats_EnvPercentile(const uint16_t *x, int n, float p);
float Stats_FloatPercentile(const float *x, int n, float p);
float Stats_MidFractionU16(const uint16_t *x, int n, float lo, float hi);
float Stats_MidFractionF(const float *x, int n, float lo, float hi);

#endif
