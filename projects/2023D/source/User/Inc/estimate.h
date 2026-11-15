#ifndef ESTIMATE_H
#define ESTIMATE_H

#include <stdint.h>
#include "mod_types.h"

void Estimate(ModType_t t, const uint16_t *env, int n_env,
              const float *freq, int n_freq, EstResult_t *out);

#endif
