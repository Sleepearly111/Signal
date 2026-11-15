#ifndef FPGA_PROTOCOL_H
#define FPGA_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FPGA_PROTOCOL_HEADER_SIZE 9U
#define FPGA_PROTOCOL_MAX_PAYLOAD 256U
#define FPGA_PROTOCOL_MAX_PACKET_SIZE \
  (FPGA_PROTOCOL_HEADER_SIZE + FPGA_PROTOCOL_MAX_PAYLOAD + 2U)

typedef struct
{
  uint8_t version;
  uint8_t kind;
  uint8_t status;
  uint16_t sequence;
  uint16_t payload_length;
  uint8_t payload[FPGA_PROTOCOL_MAX_PAYLOAD];
  uint16_t received_crc;
  uint16_t calculated_crc;
} FpgaProtocolPacket;

uint16_t FpgaProtocol_Crc16Ccitt(const uint8_t *data, size_t length);
bool FpgaProtocol_ParseHeader(const uint8_t header[FPGA_PROTOCOL_HEADER_SIZE],
                              uint16_t *payload_length);
bool FpgaProtocol_Decode(
    const uint8_t header[FPGA_PROTOCOL_HEADER_SIZE],
    const uint8_t *payload_and_crc,
    uint16_t payload_length,
    FpgaProtocolPacket *packet);

#endif
