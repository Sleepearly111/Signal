#ifndef LTC1068_CLOCK_H
#define LTC1068_CLOCK_H

#include <stdint.h>

typedef struct {
    uint32_t timer_clock_hz;
    uint32_t target_cutoff_hz;
    uint32_t clock_ratio;
    uint32_t achieved_clock_hz;
    uint32_t prescaler;
    uint32_t period;
    uint32_t pulse;
} LTC1068_ClockConfig;

int LTC1068_Clock_Compute(uint32_t timer_clock_hz,
                          uint32_t cutoff_hz,
                          uint32_t ratio,
                          LTC1068_ClockConfig *out);

#endif
