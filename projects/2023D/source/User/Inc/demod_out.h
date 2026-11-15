#ifndef DEMOD_OUT_H
#define DEMOD_OUT_H

#include <stdint.h>
#include "mod_types.h"

void DemodOut_PlayTestSine(void);
void DemodOut_PlayDemod(ModType_t t, const uint16_t *env, int n_env,
                        const float *freq, int n_freq, const EstResult_t *r);
void DemodOut_Stop(void);

#endif
