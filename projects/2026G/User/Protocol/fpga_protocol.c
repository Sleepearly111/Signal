#include "fpga_protocol.h"

#include <string.h>

uint16_t FpgaProtocol_Crc16Ccitt(const uint8_t *data, size_t length)
{
  uint16_t crc = 0xFFFFU;

  if ((data == NULL) && (length != 0U))
  {
    return 0U;
  }

  for (size_t i = 0U; i < length; ++i)
  {
    crc ^= (uint16_t)data[i] << 8;
    for (uint8_t bit = 0U; bit < 8U; ++bit)
    {
      crc = (crc & 0x8000U) != 0U
                ? (uint16_t)((crc << 1) ^ 0x1021U)
                : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

bool FpgaProtocol_IsSupportedVersion(uint8_t version)
{
  return (version == FPGA_PROTOCOL_LEGACY_VERSION) ||
         (version == FPGA_PROTOCOL_AD9280_BRINGUP_VERSION) ||
         (version == FPGA_PROTOCOL_G2026_VERSION);
}

bool FpgaProtocol_ParseHeader(const uint8_t header[FPGA_PROTOCOL_HEADER_SIZE],
                              uint16_t *payload_length)
{
  uint16_t length;

  if ((header == NULL) || (payload_length == NULL))
  {
    return false;
  }

  length = (uint16_t)header[7] | ((uint16_t)header[8] << 8);
  if ((header[0] != 0xA5U) || (header[1] != 0x5AU) ||
      !FpgaProtocol_IsSupportedVersion(header[2]) ||
      (length > FPGA_PROTOCOL_MAX_PAYLOAD))
  {
    return false;
  }

  *payload_length = length;
  return true;
}

bool FpgaProtocol_Decode(
    const uint8_t header[FPGA_PROTOCOL_HEADER_SIZE],
    const uint8_t *payload_and_crc,
    uint16_t payload_length,
    FpgaProtocolPacket *packet)
{
  uint8_t crc_input[FPGA_PROTOCOL_HEADER_SIZE + FPGA_PROTOCOL_MAX_PAYLOAD];
  uint16_t header_payload_length;

  if ((header == NULL) || (payload_and_crc == NULL) || (packet == NULL) ||
      !FpgaProtocol_ParseHeader(header, &header_payload_length) ||
      (payload_length != header_payload_length))
  {
    return false;
  }

  memcpy(crc_input, header, FPGA_PROTOCOL_HEADER_SIZE);
  memcpy(&crc_input[FPGA_PROTOCOL_HEADER_SIZE], payload_and_crc, payload_length);

  packet->version = header[2];
  packet->kind = header[3];
  packet->status = header[4];
  packet->sequence = (uint16_t)header[5] | ((uint16_t)header[6] << 8);
  packet->payload_length = payload_length;
  memcpy(packet->payload, payload_and_crc, payload_length);
  packet->received_crc =
      (uint16_t)payload_and_crc[payload_length] |
      ((uint16_t)payload_and_crc[payload_length + 1U] << 8);
  packet->calculated_crc = FpgaProtocol_Crc16Ccitt(
      crc_input, FPGA_PROTOCOL_HEADER_SIZE + payload_length);

  return packet->received_crc == packet->calculated_crc;
}
