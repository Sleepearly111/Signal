#include "fpga_packet_protocol.h"

#include <string.h>

uint16_t fpga_packet_crc16_ccitt(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFU;
    size_t i;

    if ((data == NULL) && (length != 0U)) {
        return 0U;
    }

    for (i = 0U; i < length; ++i) {
        uint8_t bit;
        crc ^= (uint16_t)data[i] << 8;
        for (bit = 0U; bit < 8U; ++bit) {
            crc = ((crc & 0x8000U) != 0U)
                      ? (uint16_t)((crc << 1) ^ 0x1021U)
                      : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

bool fpga_packet_parse_header(
    const uint8_t header[FPGA_PACKET_HEADER_SIZE],
    uint16_t *payload_length)
{
    uint16_t length;

    if ((header == NULL) || (payload_length == NULL)) {
        return false;
    }

    length = (uint16_t)header[7] | ((uint16_t)header[8] << 8);
    if ((header[0] != FPGA_PACKET_SYNC0) ||
        (header[1] != FPGA_PACKET_SYNC1) ||
        (header[2] != FPGA_PACKET_VERSION) ||
        (length > FPGA_PACKET_MAX_PAYLOAD)) {
        return false;
    }

    *payload_length = length;
    return true;
}

bool fpga_packet_decode(
    const uint8_t header[FPGA_PACKET_HEADER_SIZE],
    const uint8_t *payload_and_crc,
    uint16_t payload_length,
    fpga_packet_t *packet)
{
    uint8_t crc_input[FPGA_PACKET_HEADER_SIZE + FPGA_PACKET_MAX_PAYLOAD];
    uint16_t header_length;

    if ((header == NULL) || (payload_and_crc == NULL) || (packet == NULL) ||
        !fpga_packet_parse_header(header, &header_length) ||
        (payload_length != header_length)) {
        return false;
    }

    memcpy(crc_input, header, FPGA_PACKET_HEADER_SIZE);
    memcpy(&crc_input[FPGA_PACKET_HEADER_SIZE], payload_and_crc, payload_length);

    packet->version = header[2];
    packet->kind = header[3];
    packet->status = header[4];
    packet->sequence = (uint16_t)header[5] | ((uint16_t)header[6] << 8);
    packet->payload_length = payload_length;
    memcpy(packet->payload, payload_and_crc, payload_length);
    packet->received_crc =
        (uint16_t)payload_and_crc[payload_length] |
        ((uint16_t)payload_and_crc[payload_length + 1U] << 8);
    packet->calculated_crc = fpga_packet_crc16_ccitt(
        crc_input, FPGA_PACKET_HEADER_SIZE + payload_length);

    return packet->received_crc == packet->calculated_crc;
}
