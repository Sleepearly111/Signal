#include "ltc1068_clock.h"

int LTC1068_Clock_Compute(uint32_t timer_clock_hz,
                          uint32_t cutoff_hz,
                          uint32_t ratio,
                          LTC1068_ClockConfig *out)
{
    uint64_t desired;
    uint32_t period;

    if ((timer_clock_hz == 0U) || (cutoff_hz == 0U) || (ratio == 0U) || (out == 0)) {
        return -1;
    }

    desired = (uint64_t)cutoff_hz * ratio;
    if ((desired < 1000U) || (desired > 6000000U)) {
        return -2;
    }

    period = (uint32_t)(((uint64_t)timer_clock_hz + (desired / 2U)) / desired);
    if ((period < 2U) || (period > 65536U)) {
        return -3;
    }

    out->timer_clock_hz = timer_clock_hz;
    out->target_cutoff_hz = cutoff_hz;
    out->clock_ratio = ratio;
    out->prescaler = 0U;
    out->period = period - 1U;
    out->pulse = period / 2U;
    out->achieved_clock_hz = timer_clock_hz / period;
    return 0;
}
