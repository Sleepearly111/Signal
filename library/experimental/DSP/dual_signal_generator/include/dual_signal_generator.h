#ifndef DUAL_SIGNAL_GENERATOR_H
#define DUAL_SIGNAL_GENERATOR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SIGNAL_MODULATION_NONE = 0,
    SIGNAL_MODULATION_AM
} Signal_Modulation_Type;

typedef struct {
    Signal_Modulation_Type type;
    float freq_hz;
    float depth;
} Signal_Modulation;

typedef struct {
    float freq_hz;
    float amplitude;
    float phase_deg;
    float delay_seconds;
    float attenuation_db;
    Signal_Modulation modulation;
    float sample_rate_hz;
    size_t buffer_size;
} Signal_Config;

typedef enum {
    DUAL_SIGNAL_GENERATOR_OK = 0,
    DUAL_SIGNAL_GENERATOR_ERROR_NULL,
    DUAL_SIGNAL_GENERATOR_ERROR_SIZE,
    DUAL_SIGNAL_GENERATOR_ERROR_RANGE,
    DUAL_SIGNAL_GENERATOR_ERROR_MODULATION
} DualSignalGenerator_Status;

/*
 * Generate one hardware-independent, steady-state signal into output_buffer.
 *
 * amplitude is the carrier peak amplitude before attenuation.
 * attenuation_db is a non-negative voltage attenuation in dB.
 * delay_seconds delays both the carrier and the AM envelope.
 * output_capacity is measured in float samples and must be at least buffer_size.
 */
DualSignalGenerator_Status DualSignalGenerator_Generate(
    const Signal_Config *config,
    float *output_buffer,
    size_t output_capacity);

#ifdef __cplusplus
}
#endif

#endif /* DUAL_SIGNAL_GENERATOR_H */
