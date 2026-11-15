#include "lut_interp.h"

#include <math.h>

lut_status_t lut_interp_init(lut_interp_t *lut,
                             const double *x_table,
                             const double *y_table,
                             size_t size)
{
    size_t i;

    if (lut == 0 || x_table == 0 || y_table == 0) {
        return LUT_ERROR_NULL;
    }
    if (size < 2U) {
        return LUT_ERROR_SIZE;
    }

    for (i = 0U; i < size; ++i) {
        if (!isfinite(x_table[i]) || !isfinite(y_table[i])) {
            return LUT_ERROR_NONFINITE;
        }
        if (i > 0U && !(x_table[i] > x_table[i - 1U])) {
            return LUT_ERROR_ORDER;
        }
    }

    lut->x_table = x_table;
    lut->y_table = y_table;
    lut->size = size;
    return LUT_OK;
}

lut_status_t lut_interp_linear(const lut_interp_t *lut,
                               double x,
                               double *y)
{
    size_t low;
    size_t high;
    size_t middle;
    double x0;
    double x1;
    double y0;
    double y1;
    double fraction;
    double result;

    if (lut == 0 || y == 0 || lut->x_table == 0 || lut->y_table == 0) {
        return LUT_ERROR_NULL;
    }
    if (lut->size < 2U) {
        return LUT_ERROR_SIZE;
    }
    if (!isfinite(x)) {
        return LUT_ERROR_NONFINITE;
    }
    if (x < lut->x_table[0] || x > lut->x_table[lut->size - 1U]) {
        return LUT_ERROR_RANGE;
    }

    if (x == lut->x_table[0]) {
        *y = lut->y_table[0];
        return LUT_OK;
    }
    if (x == lut->x_table[lut->size - 1U]) {
        *y = lut->y_table[lut->size - 1U];
        return LUT_OK;
    }

    low = 0U;
    high = lut->size - 1U;
    while (high - low > 1U) {
        middle = low + (high - low) / 2U;
        if (x < lut->x_table[middle]) {
            high = middle;
        } else {
            low = middle;
        }
    }

    x0 = lut->x_table[low];
    x1 = lut->x_table[high];
    y0 = lut->y_table[low];
    y1 = lut->y_table[high];
    fraction = (x - x0) / (x1 - x0);
    result = y0 + fraction * (y1 - y0);
    if (!isfinite(result)) {
        return LUT_ERROR_NONFINITE;
    }

    *y = result;
    return LUT_OK;
}
