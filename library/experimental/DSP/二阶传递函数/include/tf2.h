#ifndef TF2_H
#define TF2_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    TF2_LOWPASS = 0,
    TF2_HIGHPASS,
    TF2_BANDPASS,
    TF2_BANDSTOP
} tf2_type_t;

typedef enum
{
    TF2_STATUS_OK = 0,
    TF2_STATUS_NULL_POINTER,
    TF2_STATUS_INVALID_TYPE,
    TF2_STATUS_INVALID_PARAMETER
} tf2_status_t;

typedef struct
{
    tf2_type_t type;
    double gain;
    double f0_hz;
    double q;
} tf2_model_t;

typedef struct
{
    double magnitude;
    double phase_rad;
} tf2_response_t;

tf2_status_t tf2_init(tf2_model_t *model,
                      tf2_type_t type,
                      double gain,
                      double f0_hz,
                      double q);

tf2_status_t tf2_calc_response(const tf2_model_t *model,
                               double frequency_hz,
                               tf2_response_t *response);

#ifdef __cplusplus
}
#endif

#endif
