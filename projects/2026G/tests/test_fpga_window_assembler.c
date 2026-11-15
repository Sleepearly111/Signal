#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fpga_window_assembler.h"

static void PutI16Le(uint8_t *data, int16_t value)
{
  const uint16_t encoded = (uint16_t)value;
  data[0] = (uint8_t)encoded;
  data[1] = (uint8_t)(encoded >> 8);
}

static void MakeChunk(G2026FpgaChunkView *chunk,
                      uint8_t *sample_bytes,
                      uint32_t acquisition_id,
                      uint8_t adc_model,
                      uint8_t channel_id,
                      uint8_t channel_count,
                      uint8_t chunk_index)
{
  const uint16_t first =
      (uint16_t)((uint16_t)chunk_index *
                 (uint16_t)G2026_FPGA_SAMPLES_PER_CHUNK);
  uint16_t count = (uint16_t)(G2026_SAMPLE_COUNT - first);

  if (count > G2026_FPGA_SAMPLES_PER_CHUNK)
  {
    count = G2026_FPGA_SAMPLES_PER_CHUNK;
  }

  memset(chunk, 0, sizeof(*chunk));
  chunk->acquisition_id = acquisition_id;
  chunk->sample_rate_hz = 2000000U;
  chunk->filter_version = G2026_FPGA_FILTER_VERSION_FIR25_TO_2;
  chunk->total_samples = G2026_SAMPLE_COUNT;
  chunk->first_sample = first;
  chunk->sample_count = count;
  chunk->chunk_index = chunk_index;
  chunk->chunk_count = G2026_FPGA_CHUNKS_PER_CHANNEL;
  chunk->adc_model = adc_model;
  chunk->channel_id = channel_id;
  chunk->channel_count = channel_count;
  chunk->channel_mask = channel_count == 1U ? 0x01U : 0x03U;
  chunk->effective_bits =
      adc_model == G2026_FPGA_ADC_AD9280 ? 8U : 14U;
  chunk->sample_format =
      G2026_FPGA_SAMPLE_FORMAT_SIGNED_I16_LE;
  chunk->source_encoding =
      G2026_FPGA_SOURCE_ENCODING_OFFSET_BINARY;
  chunk->frontend_range_id =
      channel_id == 0U ? G2026_FPGA_PATH_THS4001_INPUT
                       : G2026_FPGA_PATH_THS4001_OUTPUT;
  chunk->sample_bytes = sample_bytes;

  for (uint16_t i = 0U; i < count; ++i)
  {
    const int16_t value =
        (int16_t)((int32_t)channel_id * 5000 +
                  (int32_t)first + (int32_t)i);
    PutI16Le(&sample_bytes[2U * i], value);
  }
}

static void TestSingleChannel(void)
{
  G2026FpgaWindowAssembler assembler;
  G2026FpgaChunkView chunk;
  uint8_t bytes[2U * G2026_FPGA_SAMPLES_PER_CHUNK];
  const G2026FpgaAcquisition *result;

  G2026FpgaWindowAssembler_Init(&assembler);
  assert(!G2026FpgaWindowAssembler_IsComplete(&assembler));
  assert(G2026FpgaWindowAssembler_Get(&assembler) == NULL);

  MakeChunk(&chunk, bytes, 10U, G2026_FPGA_ADC_AD9280,
            0U, 1U, 36U);
  assert(G2026FpgaWindowAssembler_Push(&assembler, &chunk) ==
         G2026_FPGA_ASSEMBLER_CHUNK_ACCEPTED);
  assert(G2026FpgaWindowAssembler_Push(&assembler, &chunk) ==
         G2026_FPGA_ASSEMBLER_DUPLICATE);
  bytes[0] ^= 1U;
  assert(G2026FpgaWindowAssembler_Push(&assembler, &chunk) ==
         G2026_FPGA_ASSEMBLER_DUPLICATE_CONFLICT);
  bytes[0] ^= 1U;

  for (int chunk_index = 35; chunk_index >= 0; --chunk_index)
  {
    MakeChunk(&chunk, bytes, 10U, G2026_FPGA_ADC_AD9280,
              0U, 1U, (uint8_t)chunk_index);
    const G2026FpgaAssemblerStatus status =
        G2026FpgaWindowAssembler_Push(&assembler, &chunk);
    if (chunk_index == 0)
    {
      assert(status ==
             G2026_FPGA_ASSEMBLER_ACQUISITION_COMPLETE);
    }
    else
    {
      assert(status == G2026_FPGA_ASSEMBLER_CHUNK_ACCEPTED);
    }
  }

  assert(G2026FpgaWindowAssembler_IsComplete(&assembler));
  result = G2026FpgaWindowAssembler_Get(&assembler);
  assert(result != NULL);
  assert(result->channel_count == 1U);
  assert(result->channels[0].received_samples == 4096U);
  assert(result->channels[0].received_chunk_bitmap ==
         G2026_FPGA_COMPLETE_CHUNK_BITMAP);
  assert(result->channels[0].samples[0] == 0);
  assert(result->channels[0].samples[4095] == 4095);
}

static void TestMetadataFailures(void)
{
  G2026FpgaWindowAssembler assembler;
  G2026FpgaChunkView chunk;
  uint8_t bytes[2U * G2026_FPGA_SAMPLES_PER_CHUNK];

  G2026FpgaWindowAssembler_Init(&assembler);
  MakeChunk(&chunk, bytes, 20U, G2026_FPGA_ADC_AD9248,
            0U, 1U, 0U);
  assert(G2026FpgaWindowAssembler_Push(&assembler, &chunk) ==
         G2026_FPGA_ASSEMBLER_CHUNK_ACCEPTED);

  MakeChunk(&chunk, bytes, 20U, G2026_FPGA_ADC_AD9248,
            0U, 1U, 1U);
  chunk.sample_rate_hz = 1999999U;
  assert(G2026FpgaWindowAssembler_Push(&assembler, &chunk) ==
         G2026_FPGA_ASSEMBLER_METADATA_CONFLICT);

  MakeChunk(&chunk, bytes, 20U, G2026_FPGA_ADC_AD9248,
            0U, 1U, 1U);
  chunk.first_sample = 0U;
  assert(G2026FpgaWindowAssembler_Push(&assembler, &chunk) ==
         G2026_FPGA_ASSEMBLER_INVALID_METADATA);

  MakeChunk(&chunk, bytes, 20U, G2026_FPGA_ADC_AD9248,
            0U, 1U, 1U);
  chunk.sample_count--;
  assert(G2026FpgaWindowAssembler_Push(&assembler, &chunk) ==
         G2026_FPGA_ASSEMBLER_INVALID_METADATA);

  MakeChunk(&chunk, bytes, 20U, G2026_FPGA_ADC_AD9248,
            0U, 1U, 1U);
  chunk.channel_id = 2U;
  assert(G2026FpgaWindowAssembler_Push(&assembler, &chunk) ==
         G2026_FPGA_ASSEMBLER_INVALID_METADATA);

  G2026FpgaWindowAssembler_Reset(&assembler);
  MakeChunk(&chunk, bytes, 21U, G2026_FPGA_ADC_AD9248,
            0U, 1U, 0U);
  chunk.sample_rate_hz = 2048001U;
  assert(G2026FpgaWindowAssembler_Push(&assembler, &chunk) ==
         G2026_FPGA_ASSEMBLER_INVALID_METADATA);

  chunk.sample_rate_hz = 1000000U;
  assert(G2026FpgaWindowAssembler_Push(&assembler, &chunk) ==
         G2026_FPGA_ASSEMBLER_INVALID_METADATA);

  chunk.sample_rate_hz = 2000000U;
  chunk.filter_version++;
  assert(G2026FpgaWindowAssembler_Push(&assembler, &chunk) ==
         G2026_FPGA_ASSEMBLER_INVALID_METADATA);
}

static void TestDualChannel(void)
{
  G2026FpgaWindowAssembler assembler;
  G2026FpgaChunkView chunk;
  uint8_t bytes[2U * G2026_FPGA_SAMPLES_PER_CHUNK];
  const G2026FpgaAcquisition *result;

  G2026FpgaWindowAssembler_Init(&assembler);
  for (uint8_t channel_id = 1U;; --channel_id)
  {
    for (int chunk_index = 36; chunk_index >= 0; --chunk_index)
    {
      MakeChunk(&chunk, bytes, 30U, G2026_FPGA_ADC_AD9248,
                channel_id, 2U, (uint8_t)chunk_index);
      const G2026FpgaAssemblerStatus status =
          G2026FpgaWindowAssembler_Push(&assembler, &chunk);
      const bool is_final =
          (channel_id == 0U) && (chunk_index == 0);
      assert(status ==
             (is_final
                  ? G2026_FPGA_ASSEMBLER_ACQUISITION_COMPLETE
                  : G2026_FPGA_ASSEMBLER_CHUNK_ACCEPTED));
    }
    if (channel_id == 0U)
    {
      break;
    }
  }

  result = G2026FpgaWindowAssembler_Get(&assembler);
  assert(result != NULL);
  assert(result->channel_count == 2U);
  assert(result->channel_mask == 0x03U);
  assert(result->channels[0].samples[4095] == 4095);
  assert(result->channels[1].samples[0] == 5000);
  assert(result->channels[1].samples[4095] == 9095);
}

static void TestResynchronizesOnlyAtNewAcquisitionStart(void)
{
  G2026FpgaWindowAssembler assembler;
  G2026FpgaChunkView chunk;
  uint8_t bytes[2U * G2026_FPGA_SAMPLES_PER_CHUNK];
  const G2026FpgaAcquisition *result;

  G2026FpgaWindowAssembler_Init(&assembler);

  /* Simulate MCU joining an FPGA acquisition after chunks 0..19 were lost. */
  MakeChunk(&chunk, bytes, 40U, G2026_FPGA_ADC_AD9280,
            0U, 1U, 20U);
  assert(G2026FpgaWindowAssembler_Push(&assembler, &chunk) ==
         G2026_FPGA_ASSEMBLER_CHUNK_ACCEPTED);

  /* A different acquisition may not resync at an arbitrary chunk. */
  MakeChunk(&chunk, bytes, 41U, G2026_FPGA_ADC_AD9280,
            0U, 1U, 1U);
  assert(G2026FpgaWindowAssembler_Push(&assembler, &chunk) ==
         G2026_FPGA_ASSEMBLER_METADATA_CONFLICT);

  /* Its canonical first chunk is the explicit safe resync boundary. */
  MakeChunk(&chunk, bytes, 41U, G2026_FPGA_ADC_AD9280,
            0U, 1U, 0U);
  assert(G2026FpgaWindowAssembler_Push(&assembler, &chunk) ==
         G2026_FPGA_ASSEMBLER_CHUNK_ACCEPTED);

  /* Same-ID chunk zero with conflicting metadata must not reset silently. */
  chunk.sample_rate_hz = 1999999U;
  assert(G2026FpgaWindowAssembler_Push(&assembler, &chunk) ==
         G2026_FPGA_ASSEMBLER_METADATA_CONFLICT);

  for (uint8_t chunk_index = 1U;
       chunk_index < G2026_FPGA_CHUNKS_PER_CHANNEL;
       ++chunk_index)
  {
    MakeChunk(&chunk, bytes, 41U, G2026_FPGA_ADC_AD9280,
              0U, 1U, chunk_index);
    const G2026FpgaAssemblerStatus status =
        G2026FpgaWindowAssembler_Push(&assembler, &chunk);
    assert(status ==
           (chunk_index + 1U == G2026_FPGA_CHUNKS_PER_CHANNEL
                ? G2026_FPGA_ASSEMBLER_ACQUISITION_COMPLETE
                : G2026_FPGA_ASSEMBLER_CHUNK_ACCEPTED));
  }

  result = G2026FpgaWindowAssembler_Get(&assembler);
  assert(result != NULL);
  assert(result->acquisition_id == 41U);
  assert(result->channels[0].samples[0] == 0);
  assert(result->channels[0].samples[4095] == 4095);
}

int main(void)
{
  TestSingleChannel();
  TestMetadataFailures();
  TestDualChannel();
  TestResynchronizesOnlyAtNewAcquisitionStart();
  puts("g2026 fpga window assembler tests passed");
  return 0;
}
