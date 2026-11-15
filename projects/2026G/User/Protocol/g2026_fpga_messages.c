#include "g2026_fpga_messages.h"

#include <stddef.h>

static uint16_t ReadU16Le(const uint8_t *data)
{
  return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t ReadU32Le(const uint8_t *data)
{
  return (uint32_t)data[0] |
         ((uint32_t)data[1] << 8) |
         ((uint32_t)data[2] << 16) |
         ((uint32_t)data[3] << 24);
}

static bool CapabilitiesAreConsistent(const G2026FpgaChunkView *chunk)
{
  if ((chunk->channel_count == 0U) || (chunk->channel_count > 2U) ||
      (chunk->channel_id >= chunk->channel_count) ||
      ((chunk->channel_mask & (uint8_t)(1U << chunk->channel_id)) == 0U) ||
      (chunk->sample_format != G2026_FPGA_SAMPLE_FORMAT_SIGNED_I16_LE) ||
      (chunk->frontend_range_id >
       G2026_FPGA_PATH_AD8421_OUTPUT) ||
      ((chunk->channel_flags &
        (uint16_t)~G2026_FPGA_CHANNEL_FLAGS_KNOWN) != 0U))
  {
    return false;
  }

  if ((chunk->channel_count == 1U) && (chunk->channel_mask != 0x01U))
  {
    return false;
  }
  if ((chunk->channel_count == 2U) && (chunk->channel_mask != 0x03U))
  {
    return false;
  }

  if (chunk->adc_model == G2026_FPGA_ADC_AD9280)
  {
    return (chunk->effective_bits == 8U) &&
           (chunk->channel_count == 1U) &&
           (chunk->source_encoding ==
            G2026_FPGA_SOURCE_ENCODING_OFFSET_BINARY);
  }
  if (chunk->adc_model == G2026_FPGA_ADC_AD9248)
  {
    return (chunk->effective_bits == 14U) &&
           ((chunk->source_encoding ==
             G2026_FPGA_SOURCE_ENCODING_OFFSET_BINARY) ||
            (chunk->source_encoding ==
             G2026_FPGA_SOURCE_ENCODING_TWOS_COMPLEMENT));
  }
  return false;
}

bool G2026FpgaMessages_DecodeSampleChunk(
    const FpgaProtocolPacket *packet,
    G2026FpgaChunkView *chunk)
{
  uint16_t expected_payload_length;
  const uint8_t *payload;

  if ((packet == NULL) || (chunk == NULL) ||
      (packet->version != FPGA_PROTOCOL_G2026_VERSION) ||
      (packet->kind != G2026_FPGA_KIND_SAMPLE_CHUNK) ||
      (packet->status != 0U) ||
      (packet->payload_length < G2026_FPGA_CHUNK_METADATA_SIZE) ||
      (packet->payload[30] != 0U) ||
      (packet->payload[31] != 0U))
  {
    return false;
  }

  payload = packet->payload;
  chunk->acquisition_id = ReadU32Le(&payload[0]);
  chunk->sample_rate_hz = ReadU32Le(&payload[4]);
  chunk->filter_version = ReadU32Le(&payload[8]);
  chunk->total_samples = ReadU16Le(&payload[12]);
  chunk->first_sample = ReadU16Le(&payload[14]);
  chunk->sample_count = ReadU16Le(&payload[16]);
  chunk->chunk_index = payload[18];
  chunk->chunk_count = payload[19];
  chunk->adc_model = payload[20];
  chunk->channel_id = payload[21];
  chunk->channel_count = payload[22];
  chunk->channel_mask = payload[23];
  chunk->effective_bits = payload[24];
  chunk->sample_format = payload[25];
  chunk->source_encoding = payload[26];
  chunk->frontend_range_id = payload[27];
  chunk->channel_flags = ReadU16Le(&payload[28]);
  chunk->sample_bytes = &payload[G2026_FPGA_CHUNK_METADATA_SIZE];

  if ((chunk->sample_count == 0U) ||
      (chunk->sample_count > G2026_FPGA_SAMPLES_PER_CHUNK))
  {
    return false;
  }

  expected_payload_length =
      G2026_FPGA_CHUNK_METADATA_SIZE +
      (uint16_t)(chunk->sample_count * G2026_FPGA_SAMPLE_BYTES);
  if (packet->payload_length != expected_payload_length)
  {
    return false;
  }

  return CapabilitiesAreConsistent(chunk);
}

bool G2026FpgaMessages_GetSample(
    const G2026FpgaChunkView *chunk,
    uint16_t sample_index,
    int16_t *sample)
{
  uint16_t offset;
  uint16_t encoded;

  if ((chunk == NULL) || (sample == NULL) ||
      (chunk->sample_bytes == NULL) ||
      (sample_index >= chunk->sample_count))
  {
    return false;
  }

  offset = (uint16_t)(sample_index * G2026_FPGA_SAMPLE_BYTES);
  encoded = (uint16_t)chunk->sample_bytes[offset] |
            ((uint16_t)chunk->sample_bytes[offset + 1U] << 8);
  *sample = (int16_t)encoded;
  return true;
}
