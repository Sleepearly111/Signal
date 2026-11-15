#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "g2026_fpga_messages.h"

static void PutU16Le(uint8_t *data, uint16_t value)
{
  data[0] = (uint8_t)value;
  data[1] = (uint8_t)(value >> 8);
}

static void PutU32Le(uint8_t *data, uint32_t value)
{
  data[0] = (uint8_t)value;
  data[1] = (uint8_t)(value >> 8);
  data[2] = (uint8_t)(value >> 16);
  data[3] = (uint8_t)(value >> 24);
}

static void MakePacket(FpgaProtocolPacket *packet)
{
  uint8_t *payload;

  memset(packet, 0, sizeof(*packet));
  packet->version = FPGA_PROTOCOL_G2026_VERSION;
  packet->kind = G2026_FPGA_KIND_SAMPLE_CHUNK;
  packet->payload_length =
      G2026_FPGA_CHUNK_METADATA_SIZE + 4U;
  payload = packet->payload;
  PutU32Le(&payload[0], 0x12345678U);
  PutU32Le(&payload[4], 2000000U);
  PutU32Le(&payload[8], 0x01020304U);
  PutU16Le(&payload[12], 4096U);
  PutU16Le(&payload[14], 112U);
  PutU16Le(&payload[16], 2U);
  payload[18] = 1U;
  payload[19] = G2026_FPGA_CHUNKS_PER_CHANNEL;
  payload[20] = G2026_FPGA_ADC_AD9248;
  payload[21] = 1U;
  payload[22] = 2U;
  payload[23] = 0x03U;
  payload[24] = 14U;
  payload[25] = G2026_FPGA_SAMPLE_FORMAT_SIGNED_I16_LE;
  payload[26] = G2026_FPGA_SOURCE_ENCODING_OFFSET_BINARY;
  payload[27] = G2026_FPGA_PATH_THS4001_OUTPUT;
  PutU16Le(&payload[28], G2026_FPGA_CHANNEL_FLAG_OTR);
  PutU16Le(&payload[32], 0xFFFFU);
  PutU16Le(&payload[34], 1234U);
}

int main(void)
{
  FpgaProtocolPacket packet;
  G2026FpgaChunkView chunk;
  int16_t sample;

  MakePacket(&packet);
  assert(G2026FpgaMessages_DecodeSampleChunk(&packet, &chunk));
  assert(chunk.acquisition_id == 0x12345678U);
  assert(chunk.sample_rate_hz == 2000000U);
  assert(chunk.channel_id == 1U);
  assert(chunk.channel_count == 2U);
  assert(chunk.sample_count == 2U);
  assert(G2026FpgaMessages_GetSample(&chunk, 0U, &sample));
  assert(sample == -1);
  assert(G2026FpgaMessages_GetSample(&chunk, 1U, &sample));
  assert(sample == 1234);
  assert(!G2026FpgaMessages_GetSample(&chunk, 2U, &sample));

  MakePacket(&packet);
  PutU16Le(&packet.payload[28],
           G2026_FPGA_CHANNEL_FLAG_IDENTITY_UNVERIFIED);
  assert(G2026FpgaMessages_DecodeSampleChunk(&packet, &chunk));
  assert(chunk.channel_flags ==
         G2026_FPGA_CHANNEL_FLAG_IDENTITY_UNVERIFIED);
  MakePacket(&packet);
  PutU16Le(&packet.payload[28], (uint16_t)(1U << 6));
  assert(!G2026FpgaMessages_DecodeSampleChunk(&packet, &chunk));

  packet.version = FPGA_PROTOCOL_LEGACY_VERSION;
  assert(!G2026FpgaMessages_DecodeSampleChunk(&packet, &chunk));
  MakePacket(&packet);
  packet.payload_length--;
  assert(!G2026FpgaMessages_DecodeSampleChunk(&packet, &chunk));
  MakePacket(&packet);
  packet.payload[24] = 8U;
  assert(!G2026FpgaMessages_DecodeSampleChunk(&packet, &chunk));
  MakePacket(&packet);
  packet.payload[23] = 0x01U;
  assert(!G2026FpgaMessages_DecodeSampleChunk(&packet, &chunk));
  MakePacket(&packet);
  packet.payload[27] = 4U;
  assert(G2026FpgaMessages_DecodeSampleChunk(&packet, &chunk));
  MakePacket(&packet);
  packet.payload[27] = 5U;
  assert(!G2026FpgaMessages_DecodeSampleChunk(&packet, &chunk));
  MakePacket(&packet);
  packet.payload[29] = 0x80U;
  assert(!G2026FpgaMessages_DecodeSampleChunk(&packet, &chunk));
  MakePacket(&packet);
  packet.payload[30] = 1U;
  assert(!G2026FpgaMessages_DecodeSampleChunk(&packet, &chunk));

  MakePacket(&packet);
  packet.payload[20] = G2026_FPGA_ADC_AD9280;
  packet.payload[21] = 0U;
  packet.payload[22] = 1U;
  packet.payload[23] = 0x01U;
  packet.payload[24] = 8U;
  assert(G2026FpgaMessages_DecodeSampleChunk(&packet, &chunk));

  puts("g2026 fpga message tests passed");
  return 0;
}
