#ifndef RMS_CALC_H
#define RMS_CALC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    RMS_OK = 0,
    RMS_NULL,
    RMS_LENGTH_INVALID,
    RMS_NON_FINITE,
    RMS_OUTPUT_INVALID
} rms_status_t;

rms_status_t rms_calc_mean(const float *samples, size_t length, float *mean);
rms_status_t rms_calc_total(const float *samples, size_t length, float *rms);
rms_status_t rms_calc_ac(const float *samples, size_t length, float *rms);

#ifdef __cplusplus
}
#endif

#endif
