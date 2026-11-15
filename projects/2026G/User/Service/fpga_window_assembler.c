#include "fpga_window_assembler.h"

#include <string.h>

static bool ChunkCapabilitiesAreSafe(
    const G2026FpgaChunkView *chunk)
{
  if ((chunk->sample_bytes == NULL) ||
      (chunk->channel_count == 0U) ||
      (chunk->channel_count > G2026_FPGA_MAX_CHANNELS) ||
      (chunk->channel_id >= chunk->channel_count) ||
      (chunk->sample_format !=
       G2026_FPGA_SAMPLE_FORMAT_SIGNED_I16_LE))
  {
    return false;
  }
  if (((chunk->channel_count == 1U) &&
       (chunk->channel_mask != 0x01U)) ||
      ((chunk->channel_count == 2U) &&
       (chunk->channel_mask != 0x03U)))
  {
    return false;
  }
  if (chunk->adc_model == G2026_FPGA_ADC_AD9280)
  {
    return (chunk->effective_bits == 8U) &&
           (chunk->channel_count == 1U);
  }
  if (chunk->adc_model == G2026_FPGA_ADC_AD9248)
  {
    return chunk->effective_bits == 14U;
  }
  return false;
}

static bool ChunkGeometryIsCanonical(const G2026FpgaChunkView *chunk)
{
  uint16_t expected_first;
  uint16_t expected_count;

  if ((chunk->total_samples != G2026_SAMPLE_COUNT) ||
      (chunk->chunk_count != G2026_FPGA_CHUNKS_PER_CHANNEL) ||
      (chunk->chunk_index >= G2026_FPGA_CHUNKS_PER_CHANNEL))
  {
    return false;
  }

  expected_first =
      (uint16_t)((uint16_t)chunk->chunk_index *
                 (uint16_t)G2026_FPGA_SAMPLES_PER_CHUNK);
  expected_count =
      (uint16_t)(G2026_SAMPLE_COUNT - expected_first);
  if (expected_count > G2026_FPGA_SAMPLES_PER_CHUNK)
  {
    expected_count = G2026_FPGA_SAMPLES_PER_CHUNK;
  }

  return (chunk->first_sample == expected_first) &&
         (chunk->sample_count == expected_count) &&
         ((uint32_t)chunk->first_sample +
              (uint32_t)chunk->sample_count <=
          G2026_SAMPLE_COUNT);
}

static bool SamplingMetadataIsSafe(const G2026FpgaChunkView *chunk)
{
  return (chunk->filter_version ==
          G2026_FPGA_FILTER_VERSION_FIR25_TO_2) &&
         ((uint64_t)chunk->sample_rate_hz >
          2ULL * (uint64_t)G2026_MAX_INPUT_FREQUENCY_HZ) &&
         ((uint64_t)chunk->sample_rate_hz <=
          (uint64_t)chunk->total_samples *
              (uint64_t)G2026_MAX_FFT_BIN_SPACING_HZ);
}

static bool CommonMetadataMatches(
    const G2026FpgaAcquisition *acquisition,
    const G2026FpgaChunkView *chunk)
{
  return (acquisition->acquisition_id == chunk->acquisition_id) &&
         (acquisition->sample_rate_hz == chunk->sample_rate_hz) &&
         (acquisition->filter_version == chunk->filter_version) &&
         (acquisition->sample_count == chunk->total_samples) &&
         (acquisition->adc_model == chunk->adc_model) &&
         (acquisition->channel_count == chunk->channel_count) &&
         (acquisition->channel_mask == chunk->channel_mask) &&
         (acquisition->sample_format == chunk->sample_format);
}

static bool ChannelMetadataMatches(
    const G2026FpgaAssembledChannel *channel,
    const G2026FpgaChunkView *chunk)
{
  return (channel->effective_bits == chunk->effective_bits) &&
         (channel->source_encoding == chunk->source_encoding) &&
         (channel->frontend_range_id == chunk->frontend_range_id) &&
         (channel->channel_flags == chunk->channel_flags);
}

static bool AllExpectedChannelsComplete(
    const G2026FpgaAcquisition *acquisition)
{
  for (uint8_t channel_id = 0U;
       channel_id < acquisition->channel_count;
       ++channel_id)
  {
    const G2026FpgaAssembledChannel *channel =
        &acquisition->channels[channel_id];
    const uint8_t channel_bit = (uint8_t)(1U << channel_id);

    if ((acquisition->channel_mask & channel_bit) == 0U)
    {
      continue;
    }
    if (!channel->metadata_valid ||
        (channel->received_samples != G2026_SAMPLE_COUNT) ||
        (channel->received_chunk_bitmap !=
         G2026_FPGA_COMPLETE_CHUNK_BITMAP))
    {
      return false;
    }
  }
  return true;
}

static void StartAcquisition(
    G2026FpgaWindowAssembler *assembler,
    const G2026FpgaChunkView *chunk)
{
  G2026FpgaWindowAssembler_Reset(assembler);
  assembler->acquisition.acquisition_id = chunk->acquisition_id;
  assembler->acquisition.sample_rate_hz = chunk->sample_rate_hz;
  assembler->acquisition.filter_version = chunk->filter_version;
  assembler->acquisition.sample_count = chunk->total_samples;
  assembler->acquisition.adc_model = chunk->adc_model;
  assembler->acquisition.channel_count = chunk->channel_count;
  assembler->acquisition.channel_mask = chunk->channel_mask;
  assembler->acquisition.sample_format = chunk->sample_format;
  assembler->active = true;
}

void G2026FpgaWindowAssembler_Init(
    G2026FpgaWindowAssembler *assembler)
{
  G2026FpgaWindowAssembler_Reset(assembler);
}

void G2026FpgaWindowAssembler_Reset(
    G2026FpgaWindowAssembler *assembler)
{
  if (assembler != NULL)
  {
    memset(assembler, 0, sizeof(*assembler));
  }
}

G2026FpgaAssemblerStatus G2026FpgaWindowAssembler_Push(
    G2026FpgaWindowAssembler *assembler,
    const G2026FpgaChunkView *chunk)
{
  G2026FpgaAssembledChannel *channel;
  int16_t decoded[G2026_FPGA_SAMPLES_PER_CHUNK];
  uint64_t chunk_bit;

  if ((assembler == NULL) || (chunk == NULL))
  {
    return G2026_FPGA_ASSEMBLER_INVALID_ARGUMENT;
  }
  if (!ChunkCapabilitiesAreSafe(chunk) ||
      !SamplingMetadataIsSafe(chunk) ||
      !ChunkGeometryIsCanonical(chunk))
  {
    return G2026_FPGA_ASSEMBLER_INVALID_METADATA;
  }

  if (!assembler->active)
  {
    StartAcquisition(assembler, chunk);
  }
  else if (!CommonMetadataMatches(&assembler->acquisition, chunk))
  {
    /*
     * MCU reset or a lost local window can leave FPGA part-way through an
     * older acquisition. The remaining valid chunks are acknowledged and
     * discarded. A canonical channel-0/chunk-0 with a new acquisition ID is
     * the only safe automatic resynchronization point.
     *
     * Do not resync on a duplicate chunk-0 with changed metadata: that is a
     * corrupt acquisition and must remain a conflict.
     */
    if ((chunk->acquisition_id !=
         assembler->acquisition.acquisition_id) &&
        (chunk->channel_id == 0U) &&
        (chunk->chunk_index == 0U))
    {
      StartAcquisition(assembler, chunk);
    }
    else
    {
      return G2026_FPGA_ASSEMBLER_METADATA_CONFLICT;
    }
  }

  channel = &assembler->acquisition.channels[chunk->channel_id];
  if (!channel->metadata_valid)
  {
    channel->effective_bits = chunk->effective_bits;
    channel->source_encoding = chunk->source_encoding;
    channel->frontend_range_id = chunk->frontend_range_id;
    channel->channel_flags = chunk->channel_flags;
    channel->metadata_valid = true;
  }
  else if (!ChannelMetadataMatches(channel, chunk))
  {
    return G2026_FPGA_ASSEMBLER_METADATA_CONFLICT;
  }

  for (uint16_t i = 0U; i < chunk->sample_count; ++i)
  {
    if (!G2026FpgaMessages_GetSample(chunk, i, &decoded[i]))
    {
      return G2026_FPGA_ASSEMBLER_INVALID_METADATA;
    }
  }

  chunk_bit = UINT64_C(1) << chunk->chunk_index;
  if ((channel->received_chunk_bitmap & chunk_bit) != 0U)
  {
    for (uint16_t i = 0U; i < chunk->sample_count; ++i)
    {
      if (channel->samples[chunk->first_sample + i] != decoded[i])
      {
        return G2026_FPGA_ASSEMBLER_DUPLICATE_CONFLICT;
      }
    }
    return G2026_FPGA_ASSEMBLER_DUPLICATE;
  }

  memcpy(&channel->samples[chunk->first_sample], decoded,
         (size_t)chunk->sample_count * sizeof(decoded[0]));
  channel->received_chunk_bitmap |= chunk_bit;
  channel->received_samples =
      (uint16_t)(channel->received_samples + chunk->sample_count);

  if (AllExpectedChannelsComplete(&assembler->acquisition))
  {
    assembler->complete = true;
    return G2026_FPGA_ASSEMBLER_ACQUISITION_COMPLETE;
  }
  return G2026_FPGA_ASSEMBLER_CHUNK_ACCEPTED;
}

bool G2026FpgaWindowAssembler_IsComplete(
    const G2026FpgaWindowAssembler *assembler)
{
  return (assembler != NULL) && assembler->complete;
}

const G2026FpgaAcquisition *G2026FpgaWindowAssembler_Get(
    const G2026FpgaWindowAssembler *assembler)
{
  if ((assembler == NULL) || !assembler->complete)
  {
    return NULL;
  }
  return &assembler->acquisition;
}
