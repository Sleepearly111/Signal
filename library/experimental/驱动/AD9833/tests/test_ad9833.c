#include "ad9833.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

typedef struct
{
    uint16_t words[16];
    unsigned int count;
    int fail_at;
} fake_bus_t;

static int fake_write(void *context, uint16_t word)
{
    fake_bus_t *bus = (fake_bus_t *)context;
    if (bus->fail_at >= 0 && (int)bus->count == bus->fail_at) {
        return -1;
    }
    bus->words[bus->count++] = word;
    return 0;
}

static void reset_bus(fake_bus_t *bus)
{
    bus->count = 0U;
    bus->fail_at = -1;
}

static void test_frequency_frames(void)
{
    fake_bus_t bus;
    ad9833_t device;

    reset_bus(&bus);
    assert(ad9833_init(&device, 10000000U, fake_write, &bus) == AD9833_OK);
    assert(ad9833_set_frequency(&device, AD9833_FREQ0, 2500000.0F) ==
           AD9833_OK);
    assert(bus.count == 3U);
    assert(bus.words[0] == 0x2000U);
    assert(bus.words[1] == 0x4000U);
    assert(bus.words[2] == 0x5000U);

    reset_bus(&bus);
    assert(ad9833_set_frequency(&device, AD9833_FREQ1, 2500000.0F) ==
           AD9833_OK);
    assert(bus.words[1] == 0x8000U);
    assert(bus.words[2] == 0x9000U);
}

static void test_phase_and_selection(void)
{
    fake_bus_t bus;
    ad9833_t device;

    reset_bus(&bus);
    assert(ad9833_init(&device, 10000000U, fake_write, &bus) == AD9833_OK);
    assert(ad9833_set_phase(&device, AD9833_PHASE0, 90.0F) == AD9833_OK);
    assert(bus.words[0] == 0xC400U);
    assert(ad9833_set_phase(&device, AD9833_PHASE1, 180.0F) == AD9833_OK);
    assert(bus.words[1] == 0xE800U);
    assert(ad9833_select_frequency(&device, AD9833_FREQ1) == AD9833_OK);
    assert(bus.words[2] == 0x2800U);
    assert(ad9833_select_phase(&device, AD9833_PHASE1) == AD9833_OK);
    assert(bus.words[3] == 0x2C00U);
}

static void test_waveform_and_reset(void)
{
    fake_bus_t bus;
    ad9833_t device;

    reset_bus(&bus);
    assert(ad9833_init(&device, 10000000U, fake_write, &bus) == AD9833_OK);
    assert(ad9833_set_waveform(&device, AD9833_WAVE_TRIANGLE) == AD9833_OK);
    assert(bus.words[0] == 0x2002U);
    assert(ad9833_set_waveform(&device, AD9833_WAVE_SQUARE) == AD9833_OK);
    assert(bus.words[1] == 0x2028U);
    assert(ad9833_set_waveform(&device, AD9833_WAVE_SINE) == AD9833_OK);
    assert(bus.words[2] == 0x2000U);
    assert(ad9833_set_reset(&device, 1) == AD9833_OK);
    assert(bus.words[3] == 0x2100U);
    assert(ad9833_set_reset(&device, 0) == AD9833_OK);
    assert(bus.words[4] == 0x2000U);
}

static void test_errors(void)
{
    fake_bus_t bus;
    ad9833_t device;

    reset_bus(&bus);
    assert(ad9833_init(0, 10000000U, fake_write, &bus) == AD9833_NULL);
    assert(ad9833_init(&device, 0U, fake_write, &bus) ==
           AD9833_INVALID_ARGUMENT);
    assert(ad9833_init(&device, 10000000U, 0, &bus) == AD9833_NULL);
    assert(ad9833_init(&device, 10000000U, fake_write, &bus) == AD9833_OK);
    assert(ad9833_set_frequency(&device, AD9833_FREQ0, NAN) ==
           AD9833_OUT_OF_RANGE);
    assert(ad9833_set_frequency(&device, AD9833_FREQ0, 5000000.0F) ==
           AD9833_OUT_OF_RANGE);
    assert(ad9833_set_phase(&device, AD9833_PHASE0, 360.0F) ==
           AD9833_OUT_OF_RANGE);
    assert(ad9833_set_waveform(&device, (ad9833_waveform_t)99) ==
           AD9833_INVALID_ARGUMENT);

    bus.fail_at = 0;
    assert(ad9833_set_reset(&device, 1) == AD9833_BUS_ERROR);
    assert(device.control == 0x2000U);
}

int main(void)
{
    test_frequency_frames();
    test_phase_and_selection();
    test_waveform_and_reset();
    test_errors();
    puts("ad9833 tests passed");
    return 0;
}
