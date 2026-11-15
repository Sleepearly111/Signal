#ifndef LUT_INTERP_H
#define LUT_INTERP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    LUT_OK = 0,
    LUT_ERROR_NULL,
    LUT_ERROR_SIZE,
    LUT_ERROR_ORDER,
    LUT_ERROR_RANGE,
    LUT_ERROR_NONFINITE
} lut_status_t;

typedef struct
{
    const double *x_table;
    const double *y_table;
    size_t size;
} lut_interp_t;

lut_status_t lut_interp_init(lut_interp_t *lut,
                             const double *x_table,
                             const double *y_table,
                             size_t size);

lut_status_t lut_interp_linear(const lut_interp_t *lut,
                               double x,
                               double *y);

#ifdef __cplusplus
}
#endif

#endif
