#include "dual_signal_generator.h"

#include <math.h>

#define DUAL_SIGNAL_GENERATOR_TWO_PI 6.28318530717958647692f

static int DualSignalGenerator_IsFinite(float value)
{
    return isfinite(value) != 0;
}

static DualSignalGenerator_Status DualSignalGenerator_Validate(
    const Signal_Config *config,
    const float *output_buffer,
    size_t output_capacity)
{
    if ((config == NULL) || (output_buffer == NULL)) {
        return DUAL_SIGNAL_GENERATOR_ERROR_NULL;
    }

    if ((config->buffer_size == 0U) ||
        (output_capacity < config->buffer_size)) {
        return DUAL_SIGNAL_GENERATOR_ERROR_SIZE;
    }

    if (!DualSignalGenerator_IsFinite(config->freq_hz) ||
        !DualSignalGenerator_IsFinite(config->amplitude) ||
        !DualSignalGenerator_IsFinite(config->phase_deg) ||
        !DualSignalGenerator_IsFinite(config->delay_seconds) ||
        !DualSignalGenerator_IsFinite(config->attenuation_db) ||
        !DualSignalGenerator_IsFinite(config->sample_rate_hz) ||
        (config->sample_rate_hz <= 0.0f) ||
        (config->freq_hz < 0.0f) ||
        (config->freq_hz >= (0.5f * config->sample_rate_hz)) ||
        (config->amplitude < 0.0f) ||
        (config->delay_seconds < 0.0f) ||
        (config->attenuation_db < 0.0f)) {
        return DUAL_SIGNAL_GENERATOR_ERROR_RANGE;
    }

    if ((config->modulation.type != SIGNAL_MODULATION_NONE) &&
        (config->modulation.type != SIGNAL_MODULATION_AM)) {
        return DUAL_SIGNAL_GENERATOR_ERROR_MODULATION;
    }

    if (config->modulation.type == SIGNAL_MODULATION_AM) {
        if (!DualSignalGenerator_IsFinite(config->modulation.freq_hz) ||
            !DualSignalGenerator_IsFinite(config->modulation.depth) ||
            (config->modulation.freq_hz <= 0.0f) ||
            (config->modulation.freq_hz >= (0.5f * config->sample_rate_hz)) ||
            (config->modulation.depth < 0.0f) ||
            (config->modulation.depth > 1.0f)) {
            return DUAL_SIGNAL_GENERATOR_ERROR_MODULATION;
        }
    }

    return DUAL_SIGNAL_GENERATOR_OK;
}

DualSignalGenerator_Status DualSignalGenerator_Generate(
    const Signal_Config *config,
    float *output_buffer,
    size_t output_capacity)
{
    DualSignalGenerator_Status status = DualSignalGenerator_Validate(
        config, output_buffer, output_capacity);
    float attenuated_amplitude;
    float phase_radians;
    size_t index;

    if (status != DUAL_SIGNAL_GENERATOR_OK) {
        return status;
    }

    attenuated_amplitude = config->amplitude *
        powf(10.0f, -config->attenuation_db / 20.0f);
    phase_radians = config->phase_deg *
        (DUAL_SIGNAL_GENERATOR_TWO_PI / 360.0f);

    for (index = 0U; index < config->buffer_size; ++index) {
        float time_seconds = ((float)index / config->sample_rate_hz) -
            config->delay_seconds;
        float envelope = 1.0f;
        float carrier_phase = DUAL_SIGNAL_GENERATOR_TWO_PI *
            config->freq_hz * time_seconds + phase_radians;

        if (config->modulation.type == SIGNAL_MODULATION_AM) {
            envelope += config->modulation.depth *
                sinf(DUAL_SIGNAL_GENERATOR_TWO_PI *
                     config->modulation.freq_hz * time_seconds);
        }

        output_buffer[index] = attenuated_amplitude * envelope *
            sinf(carrier_phase);
    }

    return DUAL_SIGNAL_GENERATOR_OK;
}
