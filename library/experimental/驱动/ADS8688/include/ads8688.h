#ifndef ADS8688_H
#define ADS8688_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    ADS8688_OK = 0,
    ADS8688_NULL,
    ADS8688_INVALID_ARGUMENT,
    ADS8688_BUS_ERROR,
    ADS8688_VERIFY_ERROR
} ads8688_status_t;

typedef enum
{
    ADS8688_CHANNEL_0 = 0,
    ADS8688_CHANNEL_1,
    ADS8688_CHANNEL_2,
    ADS8688_CHANNEL_3,
    ADS8688_CHANNEL_4,
    ADS8688_CHANNEL_5,
    ADS8688_CHANNEL_6,
    ADS8688_CHANNEL_7
} ads8688_channel_t;

typedef int (*ads8688_transfer_fn)(void *context,
                                   const uint8_t *tx,
                                   uint8_t *rx,
                                   size_t length);

typedef struct
{
    ads8688_transfer_fn transfer;
    void *bus_context;
} ads8688_t;

#define ADS8688_COMMAND_NO_OP       0x0000U
#define ADS8688_COMMAND_STANDBY     0x8200U
#define ADS8688_COMMAND_POWER_DOWN  0x8300U
#define ADS8688_COMMAND_RESET       0x8500U
#define ADS8688_COMMAND_AUTO_RESET  0xA000U

#define ADS8688_REG_AUTO_SEQUENCE   0x01U
#define ADS8688_REG_POWER_DOWN      0x02U
#define ADS8688_REG_RANGE_CH0       0x05U

ads8688_status_t ads8688_init(ads8688_t *device,
                              ads8688_transfer_fn transfer,
                              void *bus_context);

ads8688_status_t ads8688_write_command(ads8688_t *device,
                                       uint16_t command);

ads8688_status_t ads8688_write_register(ads8688_t *device,
                                        uint8_t address,
                                        uint8_t value);

ads8688_status_t ads8688_read_register(ads8688_t *device,
                                       uint8_t address,
                                       uint8_t *value);

ads8688_status_t ads8688_verify_register(ads8688_t *device,
                                         uint8_t address,
                                         uint8_t expected,
                                         uint8_t mask);

ads8688_status_t ads8688_set_auto_sequence(ads8688_t *device,
                                           uint8_t channel_mask);

ads8688_status_t ads8688_set_power_down_mask(ads8688_t *device,
                                             uint8_t channel_mask);

ads8688_status_t ads8688_set_channel_range(ads8688_t *device,
                                           ads8688_channel_t channel,
                                           uint8_t range_config);

ads8688_status_t ads8688_enter_auto_reset(ads8688_t *device);

ads8688_status_t ads8688_select_manual_channel(ads8688_t *device,
                                               ads8688_channel_t channel);

ads8688_status_t ads8688_read_sample(ads8688_t *device,
                                     uint16_t *sample);

#ifdef __cplusplus
}
#endif

#endif
