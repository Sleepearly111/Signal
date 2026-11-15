#include "ads8688.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    uint8_t tx[8];
    uint8_t response[8];
    size_t length;
    unsigned int calls;
    int fail;
} fake_bus_t;

static int fake_transfer(void *context,
                         const uint8_t *tx,
                         uint8_t *rx,
                         size_t length)
{
    fake_bus_t *bus = (fake_bus_t *)context;
    size_t i;

    if (bus->fail != 0) {
        return -1;
    }
    bus->length = length;
    ++bus->calls;
    for (i = 0U; i < length; ++i) {
        bus->tx[i] = tx[i];
        if (rx != 0) {
            rx[i] = bus->response[i];
        }
    }
    return 0;
}

static void reset_bus(fake_bus_t *bus)
{
    memset(bus, 0, sizeof(*bus));
}

static void test_commands(void)
{
    fake_bus_t bus;
    ads8688_t device;

    reset_bus(&bus);
    assert(ads8688_init(&device, fake_transfer, &bus) == ADS8688_OK);
    assert(ads8688_enter_auto_reset(&device) == ADS8688_OK);
    assert(bus.length == 2U && bus.tx[0] == 0xA0U && bus.tx[1] == 0x00U);

    assert(ads8688_select_manual_channel(&device, ADS8688_CHANNEL_3) ==
           ADS8688_OK);
    assert(bus.tx[0] == 0xCCU && bus.tx[1] == 0x00U);
}

static void test_register_access(void)
{
    fake_bus_t bus;
    ads8688_t device;
    uint8_t value = 0U;

    reset_bus(&bus);
    assert(ads8688_init(&device, fake_transfer, &bus) == ADS8688_OK);
    assert(ads8688_write_register(&device, 0x05U, 0x71U) == ADS8688_OK);
    assert(bus.length == 2U && bus.tx[0] == 0x0BU && bus.tx[1] == 0x71U);

    bus.response[2] = 0xA5U;
    assert(ads8688_read_register(&device, 0x01U, &value) == ADS8688_OK);
    assert(bus.length == 3U && bus.tx[0] == 0x02U);
    assert(value == 0xA5U);
    assert(ads8688_verify_register(&device, 0x01U, 0xA0U, 0xF0U) ==
           ADS8688_OK);
    assert(ads8688_verify_register(&device, 0x01U, 0x50U, 0xF0U) ==
           ADS8688_VERIFY_ERROR);
}

static void test_configuration_helpers(void)
{
    fake_bus_t bus;
    ads8688_t device;

    reset_bus(&bus);
    assert(ads8688_init(&device, fake_transfer, &bus) == ADS8688_OK);
    assert(ads8688_set_auto_sequence(&device, 0x03U) == ADS8688_OK);
    assert(bus.tx[0] == 0x03U && bus.tx[1] == 0x03U);
    assert(ads8688_set_power_down_mask(&device, 0xF8U) == ADS8688_OK);
    assert(bus.tx[0] == 0x05U && bus.tx[1] == 0xF8U);
    assert(ads8688_set_channel_range(&device, ADS8688_CHANNEL_2, 0x71U) ==
           ADS8688_OK);
    assert(bus.tx[0] == 0x0FU && bus.tx[1] == 0x71U);
}

static void test_sample_read(void)
{
    fake_bus_t bus;
    ads8688_t device;
    uint16_t sample = 0U;

    reset_bus(&bus);
    bus.response[2] = 0x12U;
    bus.response[3] = 0x34U;
    assert(ads8688_init(&device, fake_transfer, &bus) == ADS8688_OK);
    assert(ads8688_read_sample(&device, &sample) == ADS8688_OK);
    assert(bus.length == 4U);
    assert(sample == 0x1234U);
}

static void test_errors_preserve_outputs(void)
{
    fake_bus_t bus;
    ads8688_t device;
    uint8_t register_value = 0x55U;
    uint16_t sample = 0x5678U;

    reset_bus(&bus);
    assert(ads8688_init(0, fake_transfer, &bus) == ADS8688_NULL);
    assert(ads8688_init(&device, 0, &bus) == ADS8688_NULL);
    assert(ads8688_init(&device, fake_transfer, &bus) == ADS8688_OK);
    assert(ads8688_write_register(&device, 0x20U, 0U) ==
           ADS8688_INVALID_ARGUMENT);
    assert(ads8688_select_manual_channel(&device, (ads8688_channel_t)8) ==
           ADS8688_INVALID_ARGUMENT);
    assert(ads8688_set_channel_range(&device, (ads8688_channel_t)8, 0U) ==
           ADS8688_INVALID_ARGUMENT);
    assert(ads8688_read_register(&device, 0x01U, 0) == ADS8688_NULL);
    assert(ads8688_read_sample(&device, 0) == ADS8688_NULL);

    bus.fail = 1;
    assert(ads8688_read_register(&device, 0x01U, &register_value) ==
           ADS8688_BUS_ERROR);
    assert(register_value == 0x55U);
    assert(ads8688_read_sample(&device, &sample) == ADS8688_BUS_ERROR);
    assert(sample == 0x5678U);
}

int main(void)
{
    test_commands();
    test_register_access();
    test_configuration_helpers();
    test_sample_read();
    test_errors_preserve_outputs();
    puts("ads8688 tests passed");
    return 0;
}
