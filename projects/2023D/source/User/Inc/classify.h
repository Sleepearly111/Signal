#ifndef CLASSIFY_H
#define CLASSIFY_H

#include <stdint.h>
#include "mod_types.h"

ModType_t Classify(const uint16_t *env, int n_env, const float *freq, int n_freq);

#endif
