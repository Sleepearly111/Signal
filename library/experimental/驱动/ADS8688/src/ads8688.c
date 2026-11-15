#include "ads8688.h"

#define ADS8688_PROGRAM_ADDRESS_MAX 0x1FU
#define ADS8688_PROGRAM_WRITE_BIT   0x01U
#define ADS8688_MANUAL_COMMAND_BASE 0xC000U

static ads8688_status_t validate_device(const ads8688_t *device)
{
    if (device == 0 || device->transfer == 0) {
        return ADS8688_NULL;
    }
    return ADS8688_OK;
}

static ads8688_status_t transfer(ads8688_t *device,
                                 const uint8_t *tx,
                                 uint8_t *rx,
                                 size_t length)
{
    return device->transfer(device->bus_context, tx, rx, length) == 0
               ? ADS8688_OK
               : ADS8688_BUS_ERROR;
}

ads8688_status_t ads8688_init(ads8688_t *device,
                              ads8688_transfer_fn transfer_callback,
                              void *bus_context)
{
    if (device == 0 || transfer_callback == 0) {
        return ADS8688_NULL;
    }
    device->transfer = transfer_callback;
    device->bus_context = bus_context;
    return ADS8688_OK;
}

ads8688_status_t ads8688_write_command(ads8688_t *device,
                                       uint16_t command)
{
    uint8_t tx[2];
    ads8688_status_t status = validate_device(device);
    if (status != ADS8688_OK) {
        return status;
    }

    tx[0] = (uint8_t)(command >> 8U);
    tx[1] = (uint8_t)command;
    return transfer(device, tx, 0, 2U);
}

ads8688_status_t ads8688_write_register(ads8688_t *device,
                                        uint8_t address,
                                        uint8_t value)
{
    uint8_t tx[2];
    ads8688_status_t status = validate_device(device);
    if (status != ADS8688_OK) {
        return status;
    }
    if (address > ADS8688_PROGRAM_ADDRESS_MAX) {
        return ADS8688_INVALID_ARGUMENT;
    }

    tx[0] = (uint8_t)((address << 1U) | ADS8688_PROGRAM_WRITE_BIT);
    tx[1] = value;
    return transfer(device, tx, 0, 2U);
}

ads8688_status_t ads8688_read_register(ads8688_t *device,
                                       uint8_t address,
                                       uint8_t *value)
{
    uint8_t tx[3] = {0U, 0U, 0U};
    uint8_t rx[3] = {0U, 0U, 0U};
    ads8688_status_t status;

    if (value == 0) {
        return ADS8688_NULL;
    }
    status = validate_device(device);
    if (status != ADS8688_OK) {
        return status;
    }
    if (address > ADS8688_PROGRAM_ADDRESS_MAX) {
        return ADS8688_INVALID_ARGUMENT;
    }

    tx[0] = (uint8_t)(address << 1U);
    status = transfer(device, tx, rx, 3U);
    if (status != ADS8688_OK) {
        return status;
    }
    *value = rx[2];
    return ADS8688_OK;
}

ads8688_status_t ads8688_verify_register(ads8688_t *device,
                                         uint8_t address,
                                         uint8_t expected,
                                         uint8_t mask)
{
    uint8_t actual;
    const ads8688_status_t status =
        ads8688_read_register(device, address, &actual);
    if (status != ADS8688_OK) {
        return status;
    }
    return (actual & mask) == (expected & mask)
               ? ADS8688_OK
               : ADS8688_VERIFY_ERROR;
}

ads8688_status_t ads8688_set_auto_sequence(ads8688_t *device,
                                           uint8_t channel_mask)
{
    return ads8688_write_register(device,
                                  ADS8688_REG_AUTO_SEQUENCE,
                                  channel_mask);
}

ads8688_status_t ads8688_set_power_down_mask(ads8688_t *device,
                                             uint8_t channel_mask)
{
    return ads8688_write_register(device,
                                  ADS8688_REG_POWER_DOWN,
                                  channel_mask);
}

ads8688_status_t ads8688_set_channel_range(ads8688_t *device,
                                           ads8688_channel_t channel,
                                           uint8_t range_config)
{
    if (channel < ADS8688_CHANNEL_0 || channel > ADS8688_CHANNEL_7) {
        return ADS8688_INVALID_ARGUMENT;
    }
    return ads8688_write_register(device,
                                  (uint8_t)(ADS8688_REG_RANGE_CH0 +
                                            (uint8_t)channel),
                                  range_config);
}

ads8688_status_t ads8688_enter_auto_reset(ads8688_t *device)
{
    return ads8688_write_command(device, ADS8688_COMMAND_AUTO_RESET);
}

ads8688_status_t ads8688_select_manual_channel(ads8688_t *device,
                                               ads8688_channel_t channel)
{
    uint16_t command;
    if (channel < ADS8688_CHANNEL_0 || channel > ADS8688_CHANNEL_7) {
        return ADS8688_INVALID_ARGUMENT;
    }
    command = (uint16_t)(ADS8688_MANUAL_COMMAND_BASE |
                         ((uint16_t)channel << 10U));
    return ads8688_write_command(device, command);
}

ads8688_status_t ads8688_read_sample(ads8688_t *device,
                                     uint16_t *sample)
{
    uint8_t tx[4] = {0U, 0U, 0U, 0U};
    uint8_t rx[4] = {0U, 0U, 0U, 0U};
    ads8688_status_t status;

    if (sample == 0) {
        return ADS8688_NULL;
    }
    status = validate_device(device);
    if (status != ADS8688_OK) {
        return status;
    }
    status = transfer(device, tx, rx, 4U);
    if (status != ADS8688_OK) {
        return status;
    }
    *sample = (uint16_t)(((uint16_t)rx[2] << 8U) | rx[3]);
    return ADS8688_OK;
}
