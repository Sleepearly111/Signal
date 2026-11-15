#include "ad603_model.h"

#include "lut_interp.h"

#include <math.h>

static ad603_status_t ad603_map_lut_status(lut_status_t status)
{
    switch (status) {
    case LUT_OK:
        return AD603_OK;
    case LUT_ERROR_NULL:
        return AD603_NULL;
    case LUT_ERROR_SIZE:
        return AD603_INVALID_SIZE;
    case LUT_ERROR_RANGE:
        return AD603_OUT_OF_RANGE;
    case LUT_ERROR_ORDER:
    case LUT_ERROR_NONFINITE:
    default:
        return AD603_INVALID_DATA;
    }
}

static ad603_gain_direction_t ad603_detect_gain_direction(
    const double *gain_table,
    size_t size)
{
    size_t i;
    ad603_gain_direction_t direction;

    if (gain_table[1] > gain_table[0]) {
        direction = AD603_GAIN_INCREASING;
    } else if (gain_table[1] < gain_table[0]) {
        direction = AD603_GAIN_DECREASING;
    } else {
        return AD603_GAIN_NON_MONOTONIC;
    }

    for (i = 2U; i < size; ++i) {
        if (direction == AD603_GAIN_INCREASING &&
            !(gain_table[i] > gain_table[i - 1U])) {
            return AD603_GAIN_NON_MONOTONIC;
        }
        if (direction == AD603_GAIN_DECREASING &&
            !(gain_table[i] < gain_table[i - 1U])) {
            return AD603_GAIN_NON_MONOTONIC;
        }
    }
    return direction;
}

static ad603_status_t ad603_validate_model(const ad603_model_t *model)
{
    if (model == 0 || model->voltage_table == 0 || model->gain_table == 0) {
        return AD603_NULL;
    }
    if (model->size < 2U) {
        return AD603_INVALID_SIZE;
    }
    if (model->gain_direction != AD603_GAIN_NON_MONOTONIC &&
        model->gain_direction != AD603_GAIN_INCREASING &&
        model->gain_direction != AD603_GAIN_DECREASING) {
        return AD603_INVALID_DATA;
    }
    return AD603_OK;
}

ad603_status_t ad603_model_init(ad603_model_t *model,
                                const double *voltage_table,
                                const double *gain_table,
                                size_t size)
{
    lut_interp_t voltage_to_gain;
    lut_status_t lut_status;
    ad603_gain_direction_t direction;

    if (model == 0 || voltage_table == 0 || gain_table == 0) {
        return AD603_NULL;
    }
    if (size < 2U) {
        return AD603_INVALID_SIZE;
    }

    lut_status = lut_interp_init(&voltage_to_gain,
                                 voltage_table,
                                 gain_table,
                                 size);
    if (lut_status != LUT_OK) {
        return ad603_map_lut_status(lut_status);
    }

    direction = ad603_detect_gain_direction(gain_table, size);
    model->voltage_table = voltage_table;
    model->gain_table = gain_table;
    model->size = size;
    model->gain_direction = direction;
    return AD603_OK;
}

ad603_status_t ad603_model_gain_from_voltage(const ad603_model_t *model,
                                             double voltage_v,
                                             double *gain_db)
{
    lut_interp_t voltage_to_gain;
    lut_status_t lut_status;
    ad603_status_t status;

    if (gain_db == 0) {
        return AD603_NULL;
    }
    status = ad603_validate_model(model);
    if (status != AD603_OK) {
        return status;
    }

    voltage_to_gain.x_table = model->voltage_table;
    voltage_to_gain.y_table = model->gain_table;
    voltage_to_gain.size = model->size;
    lut_status = lut_interp_linear(&voltage_to_gain, voltage_v, gain_db);
    return ad603_map_lut_status(lut_status);
}

ad603_status_t ad603_model_voltage_from_gain(const ad603_model_t *model,
                                             double gain_db,
                                             double *voltage_v)
{
    size_t low;
    size_t high;
    size_t middle;
    double gain_low;
    double gain_high;
    double voltage_low;
    double voltage_high;
    double fraction;
    double result;
    ad603_status_t status;

    if (voltage_v == 0) {
        return AD603_NULL;
    }
    status = ad603_validate_model(model);
    if (status != AD603_OK) {
        return status;
    }
    if (!isfinite(gain_db)) {
        return AD603_INVALID_DATA;
    }
    if (model->gain_direction == AD603_GAIN_NON_MONOTONIC) {
        return AD603_NON_MONOTONIC;
    }

    if (model->gain_direction == AD603_GAIN_INCREASING) {
        lut_interp_t gain_to_voltage;
        lut_status_t lut_status;

        gain_to_voltage.x_table = model->gain_table;
        gain_to_voltage.y_table = model->voltage_table;
        gain_to_voltage.size = model->size;
        lut_status = lut_interp_linear(&gain_to_voltage, gain_db, voltage_v);
        return ad603_map_lut_status(lut_status);
    }

    if (gain_db > model->gain_table[0] ||
        gain_db < model->gain_table[model->size - 1U]) {
        return AD603_OUT_OF_RANGE;
    }
    if (gain_db == model->gain_table[0]) {
        *voltage_v = model->voltage_table[0];
        return AD603_OK;
    }
    if (gain_db == model->gain_table[model->size - 1U]) {
        *voltage_v = model->voltage_table[model->size - 1U];
        return AD603_OK;
    }

    low = 0U;
    high = model->size - 1U;
    while (high - low > 1U) {
        middle = low + (high - low) / 2U;
        if (gain_db > model->gain_table[middle]) {
            high = middle;
        } else {
            low = middle;
        }
    }

    gain_low = model->gain_table[low];
    gain_high = model->gain_table[high];
    voltage_low = model->voltage_table[low];
    voltage_high = model->voltage_table[high];
    fraction = (gain_db - gain_low) / (gain_high - gain_low);
    result = voltage_low + fraction * (voltage_high - voltage_low);
    if (!isfinite(result)) {
        return AD603_NUMERIC_ERROR;
    }

    *voltage_v = result;
    return AD603_OK;
}

ad603_status_t ad603_gain_db_to_linear(double gain_db,
                                       double *gain_v_per_v)
{
    double result;

    if (gain_v_per_v == 0) {
        return AD603_NULL;
    }
    if (!isfinite(gain_db)) {
        return AD603_INVALID_DATA;
    }

    result = pow(10.0, gain_db / 20.0);
    if (!isfinite(result) || result <= 0.0) {
        return AD603_NUMERIC_ERROR;
    }
    *gain_v_per_v = result;
    return AD603_OK;
}

ad603_status_t ad603_gain_linear_to_db(double gain_v_per_v,
                                       double *gain_db)
{
    double result;

    if (gain_db == 0) {
        return AD603_NULL;
    }
    if (!isfinite(gain_v_per_v) || gain_v_per_v <= 0.0) {
        return AD603_INVALID_DATA;
    }

    result = 20.0 * log10(gain_v_per_v);
    if (!isfinite(result)) {
        return AD603_NUMERIC_ERROR;
    }
    *gain_db = result;
    return AD603_OK;
}
