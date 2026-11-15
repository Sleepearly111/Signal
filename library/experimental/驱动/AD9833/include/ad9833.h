#ifndef AD9833_H
#define AD9833_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    AD9833_OK = 0,
    AD9833_NULL,
    AD9833_INVALID_ARGUMENT,
    AD9833_OUT_OF_RANGE,
    AD9833_BUS_ERROR
} ad9833_status_t;

typedef enum
{
    AD9833_FREQ0 = 0,
    AD9833_FREQ1
} ad9833_frequency_register_t;

typedef enum
{
    AD9833_PHASE0 = 0,
    AD9833_PHASE1
} ad9833_phase_register_t;

typedef enum
{
    AD9833_WAVE_SINE = 0,
    AD9833_WAVE_TRIANGLE,
    AD9833_WAVE_SQUARE
} ad9833_waveform_t;

typedef int (*ad9833_write_word_fn)(void *context, uint16_t word);

typedef struct
{
    uint32_t master_clock_hz;
    ad9833_write_word_fn write_word;
    void *bus_context;
    uint16_t control;
} ad9833_t;

ad9833_status_t ad9833_init(ad9833_t *device,
                            uint32_t master_clock_hz,
                            ad9833_write_word_fn write_word,
                            void *bus_context);

ad9833_status_t ad9833_set_reset(ad9833_t *device, int enabled);

ad9833_status_t ad9833_set_frequency(ad9833_t *device,
                                     ad9833_frequency_register_t reg,
                                     float frequency_hz);

ad9833_status_t ad9833_select_frequency(ad9833_t *device,
                                        ad9833_frequency_register_t reg);

ad9833_status_t ad9833_set_phase(ad9833_t *device,
                                 ad9833_phase_register_t reg,
                                 float phase_degrees);

ad9833_status_t ad9833_select_phase(ad9833_t *device,
                                    ad9833_phase_register_t reg);

ad9833_status_t ad9833_set_waveform(ad9833_t *device,
                                    ad9833_waveform_t waveform);

#ifdef __cplusplus
}
#endif

#endif
