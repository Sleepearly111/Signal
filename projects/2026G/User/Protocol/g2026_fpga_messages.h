#ifndef G2026_FPGA_MESSAGES_H
#define G2026_FPGA_MESSAGES_H

#include <stdbool.h>
#include <stdint.h>

#include "fpga_protocol.h"

#define G2026_FPGA_KIND_SAMPLE_CHUNK 0x30U
#define G2026_FPGA_CHUNK_METADATA_SIZE 32U
#define G2026_FPGA_SAMPLE_BYTES 2U
#define G2026_FPGA_SAMPLES_PER_CHUNK \
  ((FPGA_PROTOCOL_MAX_PAYLOAD - G2026_FPGA_CHUNK_METADATA_SIZE) / \
   G2026_FPGA_SAMPLE_BYTES)
#define G2026_FPGA_CHUNKS_PER_CHANNEL 37U
#define G2026_FPGA_FILTER_VERSION_FIR25_TO_2 0x20260730UL

typedef enum
{
  G2026_FPGA_ADC_UNKNOWN = 0,
  G2026_FPGA_ADC_AD9280 = 1,
  G2026_FPGA_ADC_AD9248 = 2
} G2026FpgaAdcModel;

typedef enum
{
  G2026_FPGA_SAMPLE_FORMAT_UNKNOWN = 0,
  G2026_FPGA_SAMPLE_FORMAT_SIGNED_I16_LE = 1
} G2026FpgaSampleFormat;

typedef enum
{
  G2026_FPGA_SOURCE_ENCODING_UNKNOWN = 0,
  G2026_FPGA_SOURCE_ENCODING_OFFSET_BINARY = 1,
  G2026_FPGA_SOURCE_ENCODING_TWOS_COMPLEMENT = 2
} G2026FpgaSourceEncoding;

typedef enum
{
  G2026_FPGA_RANGE_UNKNOWN = 0,
  G2026_FPGA_PATH_THS4001_INPUT = 1,
  G2026_FPGA_PATH_THS4001_OUTPUT = 2,
  G2026_FPGA_RANGE_LEGACY_GAIN_4X = 3,
  G2026_FPGA_PATH_AD8421_OUTPUT = 4
} G2026FpgaRangeId;

enum
{
  G2026_FPGA_CHANNEL_FLAG_OTR = 1U << 0,
  G2026_FPGA_CHANNEL_FLAG_RAIL_HIT = 1U << 1,
  G2026_FPGA_CHANNEL_FLAG_FILTER_SATURATED = 1U << 2,
  G2026_FPGA_CHANNEL_FLAG_PLL_UNLOCKED = 1U << 3,
  G2026_FPGA_CHANNEL_FLAG_FIR_SCHEDULE_OVERRUN = 1U << 4,
  G2026_FPGA_CHANNEL_FLAG_IDENTITY_UNVERIFIED = 1U << 5,
  G2026_FPGA_CHANNEL_FLAGS_KNOWN =
      G2026_FPGA_CHANNEL_FLAG_OTR |
      G2026_FPGA_CHANNEL_FLAG_RAIL_HIT |
      G2026_FPGA_CHANNEL_FLAG_FILTER_SATURATED |
      G2026_FPGA_CHANNEL_FLAG_PLL_UNLOCKED |
      G2026_FPGA_CHANNEL_FLAG_FIR_SCHEDULE_OVERRUN |
      G2026_FPGA_CHANNEL_FLAG_IDENTITY_UNVERIFIED
};

typedef struct
{
  uint32_t acquisition_id;
  uint32_t sample_rate_hz;
  uint32_t filter_version;
  uint16_t total_samples;
  uint16_t first_sample;
  uint16_t sample_count;
  uint8_t chunk_index;
  uint8_t chunk_count;
  uint8_t adc_model;
  uint8_t channel_id;
  uint8_t channel_count;
  uint8_t channel_mask;
  uint8_t effective_bits;
  uint8_t sample_format;
  uint8_t source_encoding;
  uint8_t frontend_range_id;
  uint16_t channel_flags;
  const uint8_t *sample_bytes;
} G2026FpgaChunkView;

bool G2026FpgaMessages_DecodeSampleChunk(
    const FpgaProtocolPacket *packet,
    G2026FpgaChunkView *chunk);

bool G2026FpgaMessages_GetSample(
    const G2026FpgaChunkView *chunk,
    uint16_t sample_index,
    int16_t *sample);

#endif
