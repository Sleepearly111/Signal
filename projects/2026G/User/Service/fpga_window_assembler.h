#ifndef FPGA_WINDOW_ASSEMBLER_H
#define FPGA_WINDOW_ASSEMBLER_H

#include <stdbool.h>
#include <stdint.h>

#include "g2026_config.h"
#include "g2026_fpga_messages.h"

#define G2026_FPGA_MAX_CHANNELS 2U
#define G2026_FPGA_COMPLETE_CHUNK_BITMAP \
  ((UINT64_C(1) << G2026_FPGA_CHUNKS_PER_CHANNEL) - UINT64_C(1))

typedef enum
{
  G2026_FPGA_ASSEMBLER_CHUNK_ACCEPTED = 0,
  G2026_FPGA_ASSEMBLER_DUPLICATE,
  G2026_FPGA_ASSEMBLER_ACQUISITION_COMPLETE,
  G2026_FPGA_ASSEMBLER_INVALID_ARGUMENT,
  G2026_FPGA_ASSEMBLER_INVALID_METADATA,
  G2026_FPGA_ASSEMBLER_METADATA_CONFLICT,
  G2026_FPGA_ASSEMBLER_DUPLICATE_CONFLICT
} G2026FpgaAssemblerStatus;

typedef struct
{
  uint8_t effective_bits;
  uint8_t source_encoding;
  uint8_t frontend_range_id;
  uint16_t channel_flags;
  uint16_t received_samples;
  bool metadata_valid;
  /*
   * Only bits 0..36 are used. A bit is set only after the complete canonical
   * chunk has been copied into samples[].
   */
  uint64_t received_chunk_bitmap;
  int16_t samples[G2026_SAMPLE_COUNT];
} G2026FpgaAssembledChannel;

typedef struct
{
  uint32_t acquisition_id;
  uint32_t sample_rate_hz;
  uint32_t filter_version;
  uint16_t sample_count;
  uint8_t adc_model;
  uint8_t channel_count;
  uint8_t channel_mask;
  uint8_t sample_format;
  G2026FpgaAssembledChannel channels[G2026_FPGA_MAX_CHANNELS];
} G2026FpgaAcquisition;

typedef struct
{
  G2026FpgaAcquisition acquisition;
  bool active;
  bool complete;
} G2026FpgaWindowAssembler;

void G2026FpgaWindowAssembler_Init(
    G2026FpgaWindowAssembler *assembler);
void G2026FpgaWindowAssembler_Reset(
    G2026FpgaWindowAssembler *assembler);
G2026FpgaAssemblerStatus G2026FpgaWindowAssembler_Push(
    G2026FpgaWindowAssembler *assembler,
    const G2026FpgaChunkView *chunk);
bool G2026FpgaWindowAssembler_IsComplete(
    const G2026FpgaWindowAssembler *assembler);
const G2026FpgaAcquisition *G2026FpgaWindowAssembler_Get(
    const G2026FpgaWindowAssembler *assembler);

#endif
