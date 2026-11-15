#ifndef AD603_MODEL_H
#define AD603_MODEL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    AD603_OK = 0,
    AD603_NULL,
    AD603_INVALID_SIZE,
    AD603_INVALID_DATA,
    AD603_OUT_OF_RANGE,
    AD603_NON_MONOTONIC,
    AD603_NUMERIC_ERROR
} ad603_status_t;

typedef enum
{
    AD603_GAIN_NON_MONOTONIC = 0,
    AD603_GAIN_INCREASING,
    AD603_GAIN_DECREASING
} ad603_gain_direction_t;

typedef struct
{
    const double *voltage_table;
    const double *gain_table;
    size_t size;
    ad603_gain_direction_t gain_direction;
} ad603_model_t;

ad603_status_t ad603_model_init(ad603_model_t *model,
                                const double *voltage_table,
                                const double *gain_table,
                                size_t size);

ad603_status_t ad603_model_gain_from_voltage(const ad603_model_t *model,
                                             double voltage_v,
                                             double *gain_db);

ad603_status_t ad603_model_voltage_from_gain(const ad603_model_t *model,
                                             double gain_db,
                                             double *voltage_v);

ad603_status_t ad603_gain_db_to_linear(double gain_db,
                                       double *gain_v_per_v);

ad603_status_t ad603_gain_linear_to_db(double gain_v_per_v,
                                       double *gain_db);

#ifdef __cplusplus
}
#endif

#endif
