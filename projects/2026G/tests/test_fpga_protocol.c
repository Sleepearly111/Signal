#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fpga_protocol.h"

static void MakeHeader(uint8_t *header, uint8_t version,
                       uint16_t payload_length)
{
  memset(header, 0, FPGA_PROTOCOL_HEADER_SIZE);
  header[0] = 0xA5U;
  header[1] = 0x5AU;
  header[2] = version;
  header[3] = 0x30U;
  header[5] = 0x34U;
  header[6] = 0x12U;
  header[7] = (uint8_t)payload_length;
  header[8] = (uint8_t)(payload_length >> 8);
}

int main(void)
{
  static const uint8_t check_text[] = "123456789";
  uint8_t header[FPGA_PROTOCOL_HEADER_SIZE];
  uint8_t payload_and_crc[4] = {0x11U, 0x22U, 0U, 0U};
  uint8_t crc_input[FPGA_PROTOCOL_HEADER_SIZE + 2U];
  FpgaProtocolPacket packet;
  uint16_t payload_length = 0U;
  uint16_t crc;

  assert(FpgaProtocol_Crc16Ccitt(
             check_text, sizeof(check_text) - 1U) == 0x29B1U);
  assert(FpgaProtocol_Crc16Ccitt(NULL, 0U) == 0xFFFFU);
  assert(FpgaProtocol_Crc16Ccitt(NULL, 1U) == 0U);

  MakeHeader(header, FPGA_PROTOCOL_LEGACY_VERSION, 2U);
  assert(FpgaProtocol_ParseHeader(header, &payload_length));
  assert(payload_length == 2U);

  MakeHeader(header, FPGA_PROTOCOL_G2026_VERSION, 2U);
  assert(FpgaProtocol_ParseHeader(header, &payload_length));
  MakeHeader(header, FPGA_PROTOCOL_AD9280_BRINGUP_VERSION, 2U);
  assert(FpgaProtocol_ParseHeader(header, &payload_length));
  assert(!FpgaProtocol_IsSupportedVersion(0x02U));
  assert(FpgaProtocol_IsSupportedVersion(FPGA_PROTOCOL_LEGACY_VERSION));
  assert(FpgaProtocol_IsSupportedVersion(FPGA_PROTOCOL_G2026_VERSION));
  assert(FpgaProtocol_IsSupportedVersion(
      FPGA_PROTOCOL_AD9280_BRINGUP_VERSION));

  header[2] = 0x02U;
  assert(!FpgaProtocol_ParseHeader(header, &payload_length));
  MakeHeader(header, FPGA_PROTOCOL_G2026_VERSION, 257U);
  assert(!FpgaProtocol_ParseHeader(header, &payload_length));
  MakeHeader(header, FPGA_PROTOCOL_G2026_VERSION, 2U);

  memcpy(crc_input, header, FPGA_PROTOCOL_HEADER_SIZE);
  memcpy(&crc_input[FPGA_PROTOCOL_HEADER_SIZE], payload_and_crc, 2U);
  crc = FpgaProtocol_Crc16Ccitt(crc_input, sizeof(crc_input));
  payload_and_crc[2] = (uint8_t)crc;
  payload_and_crc[3] = (uint8_t)(crc >> 8);

  assert(FpgaProtocol_Decode(header, payload_and_crc, 2U, &packet));
  assert(packet.version == FPGA_PROTOCOL_G2026_VERSION);
  assert(packet.kind == 0x30U);
  assert(packet.sequence == 0x1234U);
  assert(packet.payload_length == 2U);
  assert(packet.payload[0] == 0x11U);
  assert(packet.received_crc == packet.calculated_crc);

  assert(!FpgaProtocol_Decode(header, payload_and_crc, 1U, &packet));
  payload_and_crc[2] ^= 0x01U;
  assert(!FpgaProtocol_Decode(header, payload_and_crc, 2U, &packet));

  puts("g2026 fpga protocol tests passed");
  return 0;
}
