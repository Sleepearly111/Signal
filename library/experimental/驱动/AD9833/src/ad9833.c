#include "ad9833.h"

#include <math.h>

#define AD9833_CONTROL_B28       0x2000U
#define AD9833_CONTROL_FSELECT   0x0800U
#define AD9833_CONTROL_PSELECT   0x0400U
#define AD9833_CONTROL_RESET     0x0100U
#define AD9833_CONTROL_OPBITEN   0x0020U
#define AD9833_CONTROL_DIV2      0x0008U
#define AD9833_CONTROL_MODE      0x0002U
#define AD9833_FREQ0_ADDRESS     0x4000U
#define AD9833_FREQ1_ADDRESS     0x8000U
#define AD9833_PHASE0_ADDRESS    0xC000U
#define AD9833_PHASE1_ADDRESS    0xE000U
#define AD9833_TUNING_SCALE      268435456.0

static ad9833_status_t validate_device(const ad9833_t *device)
{
    if (device == 0 || device->write_word == 0) {
        return AD9833_NULL;
    }
    if (device->master_clock_hz == 0U) {
        return AD9833_INVALID_ARGUMENT;
    }
    return AD9833_OK;
}

static ad9833_status_t write_word(ad9833_t *device, uint16_t word)
{
    return device->write_word(device->bus_context, word) == 0
               ? AD9833_OK
               : AD9833_BUS_ERROR;
}

static ad9833_status_t write_control(ad9833_t *device, uint16_t control)
{
    const ad9833_status_t status = write_word(device, control);
    if (status == AD9833_OK) {
        device->control = control;
    }
    return status;
}

ad9833_status_t ad9833_init(ad9833_t *device,
                            uint32_t master_clock_hz,
                            ad9833_write_word_fn write_callback,
                            void *bus_context)
{
    if (device == 0 || write_callback == 0) {
        return AD9833_NULL;
    }
    if (master_clock_hz == 0U) {
        return AD9833_INVALID_ARGUMENT;
    }

    device->master_clock_hz = master_clock_hz;
    device->write_word = write_callback;
    device->bus_context = bus_context;
    device->control = AD9833_CONTROL_B28;
    return AD9833_OK;
}

ad9833_status_t ad9833_set_reset(ad9833_t *device, int enabled)
{
    uint16_t control;
    ad9833_status_t status = validate_device(device);
    if (status != AD9833_OK) {
        return status;
    }

    control = device->control;
    if (enabled != 0) {
        control |= AD9833_CONTROL_RESET;
    } else {
        control &= (uint16_t)~AD9833_CONTROL_RESET;
    }
    return write_control(device, control);
}

ad9833_status_t ad9833_set_frequency(ad9833_t *device,
                                     ad9833_frequency_register_t reg,
                                     float frequency_hz)
{
    uint32_t tuning_word;
    uint16_t address;
    ad9833_status_t status = validate_device(device);
    if (status != AD9833_OK) {
        return status;
    }
    if (reg != AD9833_FREQ0 && reg != AD9833_FREQ1) {
        return AD9833_INVALID_ARGUMENT;
    }
    if (!isfinite(frequency_hz) || frequency_hz < 0.0F ||
        frequency_hz >= (float)device->master_clock_hz / 2.0F) {
        return AD9833_OUT_OF_RANGE;
    }

    tuning_word = (uint32_t)llround((double)frequency_hz *
                                    AD9833_TUNING_SCALE /
                                    (double)device->master_clock_hz);
    if (tuning_word > 0x0FFFFFFFU) {
        return AD9833_OUT_OF_RANGE;
    }
    address = reg == AD9833_FREQ0 ? AD9833_FREQ0_ADDRESS
                                  : AD9833_FREQ1_ADDRESS;

    status = write_word(device, device->control | AD9833_CONTROL_B28);
    if (status != AD9833_OK) {
        return status;
    }
    status = write_word(device, (uint16_t)(address | (tuning_word & 0x3FFFU)));
    if (status != AD9833_OK) {
        return status;
    }
    return write_word(device,
                      (uint16_t)(address | ((tuning_word >> 14U) & 0x3FFFU)));
}

ad9833_status_t ad9833_select_frequency(ad9833_t *device,
                                        ad9833_frequency_register_t reg)
{
    uint16_t control;
    ad9833_status_t status = validate_device(device);
    if (status != AD9833_OK) {
        return status;
    }
    if (reg != AD9833_FREQ0 && reg != AD9833_FREQ1) {
        return AD9833_INVALID_ARGUMENT;
    }

    control = device->control;
    if (reg == AD9833_FREQ1) {
        control |= AD9833_CONTROL_FSELECT;
    } else {
        control &= (uint16_t)~AD9833_CONTROL_FSELECT;
    }
    return write_control(device, control);
}

ad9833_status_t ad9833_set_phase(ad9833_t *device,
                                 ad9833_phase_register_t reg,
                                 float phase_degrees)
{
    uint16_t phase_word;
    uint16_t address;
    ad9833_status_t status = validate_device(device);
    if (status != AD9833_OK) {
        return status;
    }
    if (reg != AD9833_PHASE0 && reg != AD9833_PHASE1) {
        return AD9833_INVALID_ARGUMENT;
    }
    if (!isfinite(phase_degrees) || phase_degrees < 0.0F ||
        phase_degrees >= 360.0F) {
        return AD9833_OUT_OF_RANGE;
    }

    phase_word = (uint16_t)lroundf(phase_degrees * 4096.0F / 360.0F);
    phase_word &= 0x0FFFU;
    address = reg == AD9833_PHASE0 ? AD9833_PHASE0_ADDRESS
                                   : AD9833_PHASE1_ADDRESS;
    return write_word(device, (uint16_t)(address | phase_word));
}

ad9833_status_t ad9833_select_phase(ad9833_t *device,
                                    ad9833_phase_register_t reg)
{
    uint16_t control;
    ad9833_status_t status = validate_device(device);
    if (status != AD9833_OK) {
        return status;
    }
    if (reg != AD9833_PHASE0 && reg != AD9833_PHASE1) {
        return AD9833_INVALID_ARGUMENT;
    }

    control = device->control;
    if (reg == AD9833_PHASE1) {
        control |= AD9833_CONTROL_PSELECT;
    } else {
        control &= (uint16_t)~AD9833_CONTROL_PSELECT;
    }
    return write_control(device, control);
}

ad9833_status_t ad9833_set_waveform(ad9833_t *device,
                                    ad9833_waveform_t waveform)
{
    uint16_t control;
    ad9833_status_t status = validate_device(device);
    if (status != AD9833_OK) {
        return status;
    }
    if (waveform != AD9833_WAVE_SINE &&
        waveform != AD9833_WAVE_TRIANGLE &&
        waveform != AD9833_WAVE_SQUARE) {
        return AD9833_INVALID_ARGUMENT;
    }

    control = device->control;
    control &= (uint16_t)~(AD9833_CONTROL_OPBITEN |
                           AD9833_CONTROL_DIV2 |
                           AD9833_CONTROL_MODE);
    if (waveform == AD9833_WAVE_TRIANGLE) {
        control |= AD9833_CONTROL_MODE;
    } else if (waveform == AD9833_WAVE_SQUARE) {
        control |= AD9833_CONTROL_OPBITEN | AD9833_CONTROL_DIV2;
    }
    return write_control(device, control);
}
