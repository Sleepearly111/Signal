#ifndef FPGA_PACKET_PROTOCOL_H
#define FPGA_PACKET_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FPGA_PACKET_SYNC0 0xA5U
#define FPGA_PACKET_SYNC1 0x5AU
#define FPGA_PACKET_VERSION 0x01U
#define FPGA_PACKET_HEADER_SIZE 9U
#define FPGA_PACKET_MAX_PAYLOAD 256U

typedef struct
{
    uint8_t version;
    uint8_t kind;
    uint8_t status;
    uint16_t sequence;
    uint16_t payload_length;
    uint8_t payload[FPGA_PACKET_MAX_PAYLOAD];
    uint16_t received_crc;
    uint16_t calculated_crc;
} fpga_packet_t;

uint16_t fpga_packet_crc16_ccitt(const uint8_t *data, size_t length);

bool fpga_packet_parse_header(
    const uint8_t header[FPGA_PACKET_HEADER_SIZE],
    uint16_t *payload_length);

bool fpga_packet_decode(
    const uint8_t header[FPGA_PACKET_HEADER_SIZE],
    const uint8_t *payload_and_crc,
    uint16_t payload_length,
    fpga_packet_t *packet);

#endif
