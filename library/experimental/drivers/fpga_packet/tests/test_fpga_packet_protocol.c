#include "fpga_packet_protocol.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void put_crc(
    const uint8_t header[FPGA_PACKET_HEADER_SIZE],
    uint8_t *payload_and_crc,
    uint16_t payload_length)
{
    uint8_t body[FPGA_PACKET_HEADER_SIZE + FPGA_PACKET_MAX_PAYLOAD];
    uint16_t crc;

    memcpy(body, header, FPGA_PACKET_HEADER_SIZE);
    memcpy(&body[FPGA_PACKET_HEADER_SIZE], payload_and_crc, payload_length);
    crc = fpga_packet_crc16_ccitt(
        body, FPGA_PACKET_HEADER_SIZE + payload_length);
    payload_and_crc[payload_length] = (uint8_t)(crc & 0xFFU);
    payload_and_crc[payload_length + 1U] = (uint8_t)(crc >> 8);
}

static void test_fixed_vector(void)
{
    const uint8_t body[] = {
        0xA5U, 0x5AU, 0x01U, 0x10U, 0x00U, 0x00U, 0x00U, 0x08U, 0x00U,
        0xE8U, 0x03U, 0xD0U, 0x07U, 0xB8U, 0x0BU, 0xA0U, 0x0FU
    };
    assert(fpga_packet_crc16_ccitt(body, sizeof(body)) == 0x9971U);
}

static void test_ramp_packet(void)
{
    const uint8_t header[FPGA_PACKET_HEADER_SIZE] = {
        0xA5U, 0x5AU, 0x01U, 0x20U, 0x00U, 0x01U, 0x00U, 0x00U, 0x01U
    };
    uint8_t payload_and_crc[FPGA_PACKET_MAX_PAYLOAD + 2U];
    fpga_packet_t packet;
    uint16_t length = 0U;
    unsigned int i;

    for (i = 0U; i < FPGA_PACKET_MAX_PAYLOAD; ++i) {
        payload_and_crc[i] = (uint8_t)i;
    }
    put_crc(header, payload_and_crc, FPGA_PACKET_MAX_PAYLOAD);

    assert(fpga_packet_parse_header(header, &length));
    assert(length == FPGA_PACKET_MAX_PAYLOAD);
    assert(payload_and_crc[256] == 0x7AU);
    assert(payload_and_crc[257] == 0xF8U);
    assert(fpga_packet_decode(
        header, payload_and_crc, FPGA_PACKET_MAX_PAYLOAD, &packet));
    assert(packet.kind == 0x20U);
    assert(packet.sequence == 1U);
    assert(packet.payload[0] == 0U);
    assert(packet.payload[255] == 255U);
}

static void test_rejections(void)
{
    uint8_t header[FPGA_PACKET_HEADER_SIZE] = {
        0xA5U, 0x5AU, 0x01U, 0x10U, 0x00U, 0x02U, 0x00U, 0x08U, 0x00U
    };
    uint8_t payload_and_crc[10] = {0U};
    fpga_packet_t packet;
    uint16_t length;

    put_crc(header, payload_and_crc, 8U);
    assert(fpga_packet_decode(header, payload_and_crc, 8U, &packet));

    header[0] = 0U;
    assert(!fpga_packet_parse_header(header, &length));
    header[0] = FPGA_PACKET_SYNC0;
    header[2] = 2U;
    assert(!fpga_packet_parse_header(header, &length));
    header[2] = FPGA_PACKET_VERSION;
    header[7] = 0x01U;
    header[8] = 0x01U;
    assert(!fpga_packet_parse_header(header, &length));
    header[7] = 8U;
    header[8] = 0U;
    payload_and_crc[0] ^= 1U;
    assert(!fpga_packet_decode(header, payload_and_crc, 8U, &packet));
    assert(!fpga_packet_decode(header, payload_and_crc, 7U, &packet));
}

int main(void)
{
    test_fixed_vector();
    test_ramp_packet();
    test_rejections();
    puts("FPGA_PACKET protocol tests passed");
    return 0;
}
