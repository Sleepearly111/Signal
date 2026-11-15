#include "bringup_app.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fpga_link_service.h"
#include "fpga_mock_adapter.h"
#include "fpga_mock_suite.h"
#include "fpga_window_assembler.h"
#include "g2026_calibration.h"
#include "g2026_ad8421_calibration.h"
#include "g2026_config.h"
#include "g2026_dual_gain.h"
#include "g2026_display_adapter.h"
#include "g2026_fpga_messages.h"
#include "g2026_measurement_dsp.h"
#include "g2026_mock_profile.h"
#include "main.h"
#include "spi.h"
#include "usart.h"

static FpgaLinkService fpga_link;
static char debug_line[256];
#if !G2026_ENABLE_BOOT_MOCK_SELFTEST
static uint32_t packet_count;
static uint32_t bad_header_count;
static G2026FpgaWindowAssembler real_assembler;
static G2026CalibrationStore calibration_store;
static G2026DspContext real_dsp_context;
static bool real_dsp_ready;
static bool acquisition_completed_waiting_ack;

#define G2026_AD9248_FILTER_VERSION UINT32_C(0x20260730)
#define G2026_INPUT_LOAD_CORRECTION 1.500F
#define G2026_DIRECT_INPUT_CORRECTION 1.000F
#define G2026_CALIBRATION_ID UINT32_C(20260730)

#define AD9280_V05_KIND 0x24U
#define AD9280_V05_METADATA_BYTES 16U
#define AD9280_V05_CHUNK_COUNT 35U
#define AD9280_V05_SAMPLES_PER_FULL_CHUNK 120U
#define AD9280_V05_SAMPLE_FORMAT_SUM16 0x11U
#define AD9280_V05_FILTER_VERSION 0x01U
#define AD9280_V05_COMPLETE_BITMAP \
  ((UINT64_C(1) << AD9280_V05_CHUNK_COUNT) - UINT64_C(1))

typedef struct
{
  bool active;
  bool complete;
  uint16_t acquisition_id;
  uint16_t received_samples;
  uint32_t sample_rate_hz;
  uint32_t clip_count;
  uint64_t received_chunk_bitmap;
  int16_t samples[G2026_SAMPLE_COUNT];
} Ad9280V05Assembler;

static Ad9280V05Assembler ad9280_v05_assembler;
static bool ad9280_v05_completed_waiting_ack;
#endif
static void Debug_Write(const char *text, int length);

#if G2026_ENABLE_BOOT_MOCK_SELFTEST
static FpgaMockAdapter mock_adapter;
static FpgaSampleWindow mock_window;
static G2026DspContext dsp_context;

#define MOCK_PI 3.14159265358979323846F
#define MOCK_VOLTAGE_TOLERANCE_MV 5.0F
#define MOCK_FREQUENCY_TOLERANCE_HZ 1000.0F

static float CalculateExpectedRms(const FpgaMockSignalConfig *signal)
{
  float square_sum = 0.0F;
  for (uint8_t i = 0U; i < signal->component_count; ++i)
  {
    square_sum +=
        signal->components[i].peak_mv *
        signal->components[i].peak_mv;
  }
  return sqrtf(0.5F * square_sum);
}

static float CalculateExpectedVpp(const FpgaMockSignalConfig *signal)
{
  float minimum = INFINITY;
  float maximum = -INFINITY;

  for (uint32_t n = 0U; n < 4096U; ++n)
  {
    const float base_angle =
        2.0F * MOCK_PI * (float)n / 4096.0F;
    float value_mv = 0.0F;
    for (uint8_t i = 0U; i < signal->component_count; ++i)
    {
      const float order =
          signal->components[i].frequency_hz /
          signal->components[0].frequency_hz;
      const float phase =
          signal->components[i].phase_deg * MOCK_PI / 180.0F;
      value_mv +=
          signal->components[i].peak_mv *
          cosf(order * base_angle + phase);
    }
    minimum = fminf(minimum, value_mv);
    maximum = fmaxf(maximum, value_mv);
  }
  return maximum - minimum;
}

static void PrintVoltageComparison(const char *label,
                                   float expected_mv,
                                   float measured_mv,
                                   bool *passed)
{
  const float error_mv = fabsf(measured_mv - expected_mv);
  const bool item_passed =
      error_mv <= MOCK_VOLTAGE_TOLERANCE_MV;
  const long expected_x100 = lroundf(100.0F * expected_mv);
  const long measured_x100 = lroundf(100.0F * measured_mv);
  const long error_x100 = lroundf(100.0F * error_mv);
  const int length = snprintf(
      debug_line, sizeof(debug_line),
      "%s：准确=%ld.%02ldmV  测量=%ld.%02ldmV  "
      "误差=%ld.%02ldmV  [%s]\r\n",
      label,
      expected_x100 / 100L, labs(expected_x100 % 100L),
      measured_x100 / 100L, labs(measured_x100 % 100L),
      error_x100 / 100L, labs(error_x100 % 100L),
      item_passed ? "通过" : "失败");
  Debug_Write(debug_line, length);
  *passed = *passed && item_passed;
}

static void PrintComponentComparison(
    const FpgaMockComponent *expected,
    float base_frequency_hz,
    const G2026MeasuredComponent *measured,
    bool available,
    bool *passed)
{
  const long expected_frequency =
      lroundf(expected->frequency_hz);
  const long expected_peak_x100 =
      lroundf(100.0F * expected->peak_mv);
  const long order =
      lroundf(expected->frequency_hz /
              base_frequency_hz);
  int length;

  if (!available)
  {
    length = snprintf(
        debug_line, sizeof(debug_line),
        "%ld次分量：准确=%ldHz/%ld.%02ldmV  测量=未检出  [失败]\r\n",
        order, expected_frequency,
        expected_peak_x100 / 100L,
        labs(expected_peak_x100 % 100L));
    Debug_Write(debug_line, length);
    *passed = false;
    return;
  }

  const float frequency_error =
      fabsf(measured->frequency_hz - expected->frequency_hz);
  const float peak_error =
      fabsf(measured->peak_mv - expected->peak_mv);
  const bool frequency_passed =
      frequency_error <= MOCK_FREQUENCY_TOLERANCE_HZ;
  const bool peak_passed =
      peak_error <= MOCK_VOLTAGE_TOLERANCE_MV;
  const long measured_peak_x100 =
      lroundf(100.0F * measured->peak_mv);
  const long peak_error_x100 =
      lroundf(100.0F * peak_error);

  length = snprintf(
      debug_line, sizeof(debug_line),
      "%ld次频率：准确=%ldHz  原始=%ldHz  修正=%ldHz  "
      "误差=%ldHz  [%s]\r\n",
      order, expected_frequency,
      lroundf(measured->raw_frequency_hz),
      lroundf(measured->frequency_hz),
      lroundf(frequency_error),
      frequency_passed ? "通过" : "失败");
  Debug_Write(debug_line, length);

  length = snprintf(
      debug_line, sizeof(debug_line),
      "%ld次幅值：准确=%ld.%02ldmV  测量=%ld.%02ldmV  "
      "误差=%ld.%02ldmV  [%s]\r\n",
      order,
      expected_peak_x100 / 100L,
      labs(expected_peak_x100 % 100L),
      measured_peak_x100 / 100L,
      labs(measured_peak_x100 % 100L),
      peak_error_x100 / 100L,
      labs(peak_error_x100 % 100L),
      peak_passed ? "通过" : "失败");
  Debug_Write(debug_line, length);
  *passed = *passed && frequency_passed && peak_passed;
}

static bool RunOneMockTest(size_t index,
                           const FpgaMockTestCase *test_case)
{
  FpgaDataSource source;
  G2026DspResult result;
  const G2026DspConfig config = {
      .millivolts_per_code =
          test_case->signal.millivolts_per_code,
      .zero_code = 0.0F,
      .minimum_frequency_hz = 10000.0F,
      .maximum_frequency_hz = 500000.0F,
      .minimum_peak_mv = 1.0F,
      .noise_multiplier = 6.0F,
      .frequency_grid_hz = 500.0F,
      .grid_tolerance_hz = 200.0F,
      .harmonic_tolerance_hz = 200.0F,
  };
  const float expected_rms_mv =
      CalculateExpectedRms(&test_case->signal);
  const float expected_vpp_mv =
      CalculateExpectedVpp(&test_case->signal);
  uint32_t started_ms;
  uint32_t elapsed_ms;
  bool passed = true;
  int length;

  FpgaMockAdapter_Init(&mock_adapter);
  if (!FpgaMockAdapter_Configure(&mock_adapter,
                                 &test_case->signal))
  {
    return false;
  }
  source = FpgaMockAdapter_MakeSource(&mock_adapter);
  started_ms = HAL_GetTick();
  if (source.start(source.context, 0U, started_ms) != FPGA_SOURCE_OK)
  {
    return false;
  }
  source.service(source.context, HAL_GetTick());
  if (source.get_window(source.context, &mock_window) != FPGA_SOURCE_OK)
  {
    return false;
  }
  const G2026DspStatus status = G2026Dsp_Analyze(
      &dsp_context,
      mock_window.samples,
      mock_window.sample_count,
      mock_window.sample_rate_hz,
      mock_window.clipped,
      mock_window.overflow,
      &config,
      &result);
  elapsed_ms = HAL_GetTick() - started_ms;

  length = snprintf(
      debug_line, sizeof(debug_line),
      "\r\n========== 测试%u/%u：%s ==========\r\n",
      (unsigned int)(index + 1U),
      (unsigned int)FpgaMockSuite_Count(),
      test_case->name);
  Debug_Write(debug_line, length);

  length = snprintf(
      debug_line, sizeof(debug_line),
      "误差设置：实际采样率=%luHz  上报=%luHz  "
      "增益=%ldppm  噪声峰值=%ld.%02ldmV\r\n",
      (unsigned long)test_case->signal.actual_sample_rate_hz,
      (unsigned long)test_case->signal.reported_sample_rate_hz,
      lroundf(test_case->signal.gain_error_ppm),
      lroundf(100.0F * test_case->signal.noise_peak_mv) / 100L,
      labs(lroundf(100.0F *
                   test_case->signal.noise_peak_mv) %
           100L));
  Debug_Write(debug_line, length);

  if (status != G2026_DSP_OK)
  {
    length = snprintf(debug_line, sizeof(debug_line),
                      "分析失败：状态码=%u\r\n结论：[失败]\r\n",
                      (unsigned int)status);
    Debug_Write(debug_line, length);
    return false;
  }

  passed =
      result.component_count == test_case->signal.component_count;
  for (uint8_t i = 0U; i < test_case->signal.component_count; ++i)
  {
    PrintComponentComparison(
        &test_case->signal.components[i],
        test_case->signal.components[0].frequency_hz,
        i < result.component_count ? &result.components[i] : NULL,
        i < result.component_count,
        &passed);
  }

  PrintVoltageComparison("真有效值", expected_rms_mv,
                         result.ac_rms_mv, &passed);
  PrintVoltageComparison(
      "整体峰峰值", expected_vpp_mv,
      result.reconstructed_vpp_valid
          ? result.reconstructed_vpp_mv
          : result.time_vpp_mv,
      &passed);

  const bool time_passed =
      elapsed_ms <= G2026_MEASUREMENT_TIMEOUT_MS;
  passed = passed && time_passed;
  length = snprintf(
      debug_line, sizeof(debug_line),
      "处理时间：%lums / 2000ms  [%s]\r\n"
      "本组结论：[%s]\r\n",
      (unsigned long)elapsed_ms,
      time_passed ? "通过" : "失败",
      passed ? "通过" : "失败");
  Debug_Write(debug_line, length);
  return passed;
}

static void RunDspMockSelftest(void)
{
  size_t passed_count = 0U;
  const size_t test_count = FpgaMockSuite_Count();

  if (G2026Dsp_Init(&dsp_context) != G2026_DSP_OK)
  {
    Debug_Write("DSP初始化失败\r\n",
                sizeof("DSP初始化失败\r\n") - 1);
    return;
  }

  Debug_Write("\r\n2026G STM32多组采样数据测试开始\r\n",
              sizeof("\r\n2026G STM32多组采样数据测试开始\r\n") - 1);
  for (size_t i = 0U; i < test_count; ++i)
  {
    const FpgaMockTestCase *test_case = FpgaMockSuite_Get(i);
    if ((test_case != NULL) && RunOneMockTest(i, test_case))
    {
      ++passed_count;
    }
  }

  const int length = snprintf(
      debug_line, sizeof(debug_line),
      "\r\n========== 总结 ==========\r\n"
      "通过：%u/%u组\r\n"
      "结论：STM32接收的是4096点原始采样码，"
      "所有结果均由STM32计算。\r\n",
      (unsigned int)passed_count,
      (unsigned int)test_count);
  Debug_Write(debug_line, length);
}
#endif

static void Debug_Write(const char *text, int length)
{
  if (length > 0)
  {
    if ((size_t)length >= sizeof(debug_line))
    {
      length = (int)sizeof(debug_line) - 1;
    }
    (void)HAL_UART_Transmit(&huart2, (uint8_t *)text, (uint16_t)length, 100U);
  }
}

#if !G2026_ENABLE_BOOT_MOCK_SELFTEST
static uint16_t ReadLe16(const uint8_t *data)
{
  return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t ReadLe32(const uint8_t *data)
{
  return (uint32_t)data[0] |
         ((uint32_t)data[1] << 8) |
         ((uint32_t)data[2] << 16) |
         ((uint32_t)data[3] << 24);
}

static void Ad9280V05_Reset(void)
{
  memset(&ad9280_v05_assembler, 0, sizeof(ad9280_v05_assembler));
}

static bool Ad9280V05_AcceptChunk(const FpgaProtocolPacket *packet)
{
  const uint8_t *payload;
  uint16_t acquisition_id;
  uint8_t chunk_index;
  uint8_t chunk_count;
  uint16_t first_sample;
  uint16_t sample_count;
  uint32_t sample_rate_hz;
  uint16_t clip_count;
  uint16_t expected_first;
  uint16_t expected_count;
  uint16_t expected_payload_length;
  uint64_t chunk_bit;

  if ((packet == NULL) ||
      (packet->version != FPGA_PROTOCOL_AD9280_BRINGUP_VERSION) ||
      (packet->kind != AD9280_V05_KIND) ||
      (packet->payload_length < AD9280_V05_METADATA_BYTES))
  {
    return false;
  }

  payload = packet->payload;
  acquisition_id = ReadLe16(&payload[0]);
  chunk_index = payload[2];
  chunk_count = payload[3];
  first_sample = ReadLe16(&payload[4]);
  sample_count = ReadLe16(&payload[6]);
  sample_rate_hz = ReadLe32(&payload[8]);
  clip_count = ReadLe16(&payload[14]);

  if ((chunk_count != AD9280_V05_CHUNK_COUNT) ||
      (chunk_index >= AD9280_V05_CHUNK_COUNT) ||
      (payload[12] != AD9280_V05_FILTER_VERSION) ||
      (payload[13] != AD9280_V05_SAMPLE_FORMAT_SUM16) ||
      (sample_rate_hz != G2026_NOMINAL_SAMPLE_RATE_HZ))
  {
    return false;
  }

  expected_first =
      (uint16_t)((uint16_t)chunk_index *
                 AD9280_V05_SAMPLES_PER_FULL_CHUNK);
  expected_count =
      chunk_index + 1U < AD9280_V05_CHUNK_COUNT
          ? AD9280_V05_SAMPLES_PER_FULL_CHUNK
          : (uint16_t)(G2026_SAMPLE_COUNT - expected_first);
  expected_payload_length =
      (uint16_t)(AD9280_V05_METADATA_BYTES +
                 expected_count * sizeof(int16_t));
  if ((first_sample != expected_first) ||
      (sample_count != expected_count) ||
      (packet->payload_length != expected_payload_length))
  {
    return false;
  }

  if (!ad9280_v05_assembler.active)
  {
    /*
     * After an MCU reset, acknowledge and discard the remainder of an old
     * FPGA acquisition. Chunk zero is the only unambiguous resync point.
     */
    if (chunk_index != 0U)
    {
      return true;
    }
    Ad9280V05_Reset();
    ad9280_v05_assembler.active = true;
    ad9280_v05_assembler.acquisition_id = acquisition_id;
    ad9280_v05_assembler.sample_rate_hz = sample_rate_hz;
  }
  else if (acquisition_id != ad9280_v05_assembler.acquisition_id)
  {
    if (chunk_index != 0U)
    {
      return true;
    }
    Ad9280V05_Reset();
    ad9280_v05_assembler.active = true;
    ad9280_v05_assembler.acquisition_id = acquisition_id;
    ad9280_v05_assembler.sample_rate_hz = sample_rate_hz;
  }

  chunk_bit = UINT64_C(1) << chunk_index;
  if ((ad9280_v05_assembler.received_chunk_bitmap & chunk_bit) != 0U)
  {
    for (uint16_t i = 0U; i < sample_count; ++i)
    {
      const uint16_t offset =
          (uint16_t)(AD9280_V05_METADATA_BYTES + 2U * i);
      const int16_t sample = (int16_t)ReadLe16(&payload[offset]);
      if (ad9280_v05_assembler.samples[first_sample + i] != sample)
      {
        return false;
      }
    }
    return true;
  }

  for (uint16_t i = 0U; i < sample_count; ++i)
  {
    const uint16_t offset =
        (uint16_t)(AD9280_V05_METADATA_BYTES + 2U * i);
    ad9280_v05_assembler.samples[first_sample + i] =
        (int16_t)ReadLe16(&payload[offset]);
  }
  ad9280_v05_assembler.received_chunk_bitmap |= chunk_bit;
  ad9280_v05_assembler.received_samples =
      (uint16_t)(ad9280_v05_assembler.received_samples + sample_count);
  ad9280_v05_assembler.clip_count += clip_count;

  if ((ad9280_v05_assembler.received_chunk_bitmap ==
       AD9280_V05_COMPLETE_BITMAP) &&
      (ad9280_v05_assembler.received_samples == G2026_SAMPLE_COUNT))
  {
    ad9280_v05_assembler.complete = true;
    ad9280_v05_completed_waiting_ack = true;
  }
  return true;
}

static void Ad9280V05_ReportWindow(void)
{
  int16_t minimum = INT16_MAX;
  int16_t maximum = INT16_MIN;
  int64_t sum = 0;

  if (!ad9280_v05_assembler.complete)
  {
    return;
  }
  for (uint16_t i = 0U; i < G2026_SAMPLE_COUNT; ++i)
  {
    const int16_t sample = ad9280_v05_assembler.samples[i];
    if (sample < minimum)
    {
      minimum = sample;
    }
    if (sample > maximum)
    {
      maximum = sample;
    }
    sum += sample;
  }

  const int length = snprintf(
      debug_line, sizeof(debug_line),
      "[adc-v05] WINDOW READY acq=%u n=%u fs=%lu sum16_min=%d "
      "sum16_max=%d mean_x100=%ld clip=%lu "
      "first=%d,%d,%d,%d\r\n",
      (unsigned int)ad9280_v05_assembler.acquisition_id,
      (unsigned int)ad9280_v05_assembler.received_samples,
      (unsigned long)ad9280_v05_assembler.sample_rate_hz,
      (int)minimum,
      (int)maximum,
      (long)((sum * 100) / (int64_t)G2026_SAMPLE_COUNT),
      (unsigned long)ad9280_v05_assembler.clip_count,
      (int)ad9280_v05_assembler.samples[0],
      (int)ad9280_v05_assembler.samples[1],
      (int)ad9280_v05_assembler.samples[2],
      (int)ad9280_v05_assembler.samples[3]);
  Debug_Write(debug_line, length);
  Debug_Write(
      "[adc-v05] unit=sum of 16 centered AD9280 codes; divide by 16 "
      "for average centered code\r\n",
      sizeof(
          "[adc-v05] unit=sum of 16 centered AD9280 codes; divide by 16 "
          "for average centered code\r\n") -
          1);

  Ad9280V05_Reset();
  ad9280_v05_completed_waiting_ack = false;
}

static void ReportPacket(const FpgaProtocolPacket *packet)
{
  int length;

  if ((packet->kind == 0x10U) && (packet->payload_length == 8U))
  {
    length = snprintf(debug_line, sizeof(debug_line),
                      "[fpga] OK seq=%u kind=10 len=8 values=%u,%u,%u,%u "
                      "crc=%04X\r\n",
                      packet->sequence,
                      ReadLe16(&packet->payload[0]),
                      ReadLe16(&packet->payload[2]),
                      ReadLe16(&packet->payload[4]),
                      ReadLe16(&packet->payload[6]),
                      packet->received_crc);
  }
  else if (((packet->version == FPGA_PROTOCOL_LEGACY_VERSION) &&
            (packet->kind == 0x20U)) &&
           (packet->payload_length > 0U))
  {
    uint8_t minimum = packet->payload[0];
    uint8_t maximum = packet->payload[0];
    uint32_t sum = 0U;

    for (uint16_t i = 0U; i < packet->payload_length; ++i)
    {
      uint8_t sample = packet->payload[i];
      if (sample < minimum)
      {
        minimum = sample;
      }
      if (sample > maximum)
      {
        maximum = sample;
      }
      sum += sample;
    }

    uint32_t average_x100 =
        (sum * 100U) / (uint32_t)packet->payload_length;
    length = snprintf(
        debug_line, sizeof(debug_line),
        "[adc] v=%02X kind=%02X seq=%u n=%u min=%02X max=%02X pp=%u "
        "avg=%u.%02u "
        "first=%02X,%02X,%02X,%02X crc=%04X\r\n",
        packet->version,
        packet->kind,
        packet->sequence,
        packet->payload_length,
        minimum,
        maximum,
        (uint16_t)maximum - (uint16_t)minimum,
        (unsigned int)(average_x100 / 100U),
        (unsigned int)(average_x100 % 100U),
        packet->payload[0],
        packet->payload_length > 1U ? packet->payload[1] : 0U,
        packet->payload_length > 2U ? packet->payload[2] : 0U,
        packet->payload_length > 3U ? packet->payload[3] : 0U,
        packet->received_crc);
  }
  else
  {
    length = snprintf(debug_line, sizeof(debug_line),
                      "[fpga] OK seq=%u kind=%02X len=%u first=%02X,%02X,"
                      "%02X,%02X crc=%04X\r\n",
                      packet->sequence,
                      packet->kind,
                      packet->payload_length,
                      packet->payload_length > 0U ? packet->payload[0] : 0U,
                      packet->payload_length > 1U ? packet->payload[1] : 0U,
                      packet->payload_length > 2U ? packet->payload[2] : 0U,
                      packet->payload_length > 3U ? packet->payload[3] : 0U,
                      packet->received_crc);
  }
  Debug_Write(debug_line, length);
}

static void ReportBadHeader(void)
{
  const uint8_t *header = FpgaLinkService_LastHeader(&fpga_link);
  int length = snprintf(
      debug_line, sizeof(debug_line),
      "[fpga] ERROR bad header raw=%02X %02X %02X %02X %02X %02X %02X "
      "%02X %02X; no ACK\r\n",
      header[0], header[1], header[2], header[3], header[4],
      header[5], header[6], header[7], header[8]);

  Debug_Write(debug_line, length);
}

static bool ChunkMatchesBuildProfile(const G2026FpgaChunkView *chunk)
{
  uint8_t expected_adc;
  uint8_t expected_channels;
  uint8_t expected_range;

#if G2026_HW_VARIANT == G2026_HW_VARIANT_AD9280_SINGLE
  expected_adc = G2026_FPGA_ADC_AD9280;
  expected_channels = 1U;
#elif G2026_HW_VARIANT == G2026_HW_VARIANT_AD9248_SINGLE
  expected_adc = G2026_FPGA_ADC_AD9248;
  expected_channels = 1U;
#elif G2026_HW_VARIANT == G2026_HW_VARIANT_AD9248_SINGLE_AD8421
  expected_adc = G2026_FPGA_ADC_AD9248;
  expected_channels = 1U;
#else
  expected_adc = G2026_FPGA_ADC_AD9248;
  expected_channels = 2U;
#endif

  if ((chunk->adc_model != expected_adc) ||
      (chunk->channel_count != expected_channels))
  {
    return false;
  }

  expected_range =
      chunk->channel_id == 0U
          ? G2026_CHANNEL0_FRONTEND_RANGE
          : G2026_CHANNEL1_FRONTEND_RANGE;
  /*
   * UNKNOWN is an explicit identity for the current direct INA path, not a
   * wildcard. Accepting 1x/2x/4x metadata here could pair a packet from an
   * obsolete bitstream with the wrong calibration record.
   */
  return chunk->frontend_range_id == expected_range;
}

static bool AcceptUnifiedChunk(const FpgaProtocolPacket *packet)
{
  G2026FpgaChunkView chunk;
  G2026FpgaAssemblerStatus status;
  int length;

  if (!G2026FpgaMessages_DecodeSampleChunk(packet, &chunk))
  {
    length = snprintf(
        debug_line, sizeof(debug_line),
        "[fpga] REJECT decode v=%u kind=%u payload=%u\r\n",
        packet != NULL ? (unsigned int)packet->version : 0U,
        packet != NULL ? (unsigned int)packet->kind : 0U,
        packet != NULL ? (unsigned int)packet->payload_length : 0U);
    Debug_Write(debug_line, length);
    return false;
  }

  if (!ChunkMatchesBuildProfile(&chunk))
  {
    length = snprintf(
        debug_line, sizeof(debug_line),
        "[fpga] REJECT profile adc=%u ch=%u/%u mask=%02X range=%u "
        "expect_hw=%u r0=%u r1=%u fs=%lu filter=%08lX\r\n",
        (unsigned int)chunk.adc_model,
        (unsigned int)chunk.channel_id,
        (unsigned int)chunk.channel_count,
        (unsigned int)chunk.channel_mask,
        (unsigned int)chunk.frontend_range_id,
        (unsigned int)G2026_HW_VARIANT,
        (unsigned int)G2026_CHANNEL0_FRONTEND_RANGE,
        (unsigned int)G2026_CHANNEL1_FRONTEND_RANGE,
        (unsigned long)chunk.sample_rate_hz,
        (unsigned long)chunk.filter_version);
    Debug_Write(debug_line, length);
    return false;
  }

  status = G2026FpgaWindowAssembler_Push(&real_assembler, &chunk);
  if (status == G2026_FPGA_ASSEMBLER_DUPLICATE_CONFLICT)
  {
    G2026FpgaWindowAssembler_Reset(&real_assembler);
    status = G2026FpgaWindowAssembler_Push(&real_assembler, &chunk);
  }
  if ((status != G2026_FPGA_ASSEMBLER_CHUNK_ACCEPTED) &&
      (status != G2026_FPGA_ASSEMBLER_DUPLICATE) &&
      (status != G2026_FPGA_ASSEMBLER_ACQUISITION_COMPLETE))
  {
    length = snprintf(debug_line, sizeof(debug_line),
                      "[fpga] REJECT assembler_status=%u acq=%lu ch=%u "
                      "chunk=%u\r\n",
                      (unsigned int)status,
                      (unsigned long)chunk.acquisition_id,
                      (unsigned int)chunk.channel_id,
                      (unsigned int)chunk.chunk_index);
    Debug_Write(debug_line, length);
    return false;
  }

  if (status == G2026_FPGA_ASSEMBLER_ACQUISITION_COMPLETE)
  {
    acquisition_completed_waiting_ack = true;
  }
  return true;
}

static bool LoadMeasuredAd9248Calibration(void)
{
  const G2026CalibrationRecord records[3] = {
      {
          /*
           * Direct generator -> INA baseline measured after warm-up with the
           * oscilloscope removed.  The scale is the through-origin fit of the
           * complete 10 kHz amplitude sweep.  Zero is the median of 100 stable
           * windows with the SDG2082X actively driving 0 V DC into its stated
           * 50-ohm load.  It already maps codes to the generator-input value,
           * so the direct path must not apply the historical 1.5 correction.
           */
          .key = {2U, 0U, G2026_FRONTEND_RANGE_UNKNOWN,
                  G2026_AD9248_FILTER_VERSION},
          .input_mv_per_code = 0.149502050F,
          .zero_code = 70.80F,
          .sample_clock_ppm = 0.0F,
          .calibration_id = UINT32_C(26080102),
          .valid = true,
      },
      {
          .key = {2U, 0U, G2026_FRONTEND_PATH_THS4001_INPUT,
                  G2026_AD9248_FILTER_VERSION},
          .input_mv_per_code = 0.1496F,
          .zero_code = 17.60F,
          .sample_clock_ppm = 0.0F,
          .calibration_id = G2026_CALIBRATION_ID,
          .valid = true,
      },
      {
          .key = {2U, 1U, G2026_FRONTEND_PATH_THS4001_OUTPUT,
                  G2026_AD9248_FILTER_VERSION},
          .input_mv_per_code = 0.1502F,
          .zero_code = 793.71F,
          .sample_clock_ppm = 0.0F,
          .calibration_id = G2026_CALIBRATION_ID,
          .valid = true,
      },
  };
  return (G2026Calibration_Put(&calibration_store, &records[0]) ==
          G2026_CALIBRATION_OK) &&
         (G2026Calibration_Put(&calibration_store, &records[1]) ==
          G2026_CALIBRATION_OK) &&
         (G2026Calibration_Put(&calibration_store, &records[2]) ==
          G2026_CALIBRATION_OK);
}

static G2026DspStatus AnalyzeAcquisitionChannel(
    const G2026FpgaAcquisition *acquisition,
    uint8_t channel_id,
    const G2026CalibrationRecord *calibration,
    G2026DspResult *result)
{
  float corrected_sample_rate_hz;
  const G2026DspConfig config = {
      .millivolts_per_code = calibration->input_mv_per_code,
      .zero_code = calibration->zero_code,
      .minimum_frequency_hz = 10000.0F,
      .maximum_frequency_hz = 500000.0F,
      .minimum_peak_mv = 1.0F,
      .noise_multiplier = 6.0F,
      .frequency_grid_hz = 500.0F,
      .grid_tolerance_hz = 200.0F,
      .harmonic_tolerance_hz = 200.0F,
  };

  if (!G2026Calibration_CorrectSampleRate(
          calibration, acquisition->sample_rate_hz,
          &corrected_sample_rate_hz))
  {
    return G2026_DSP_INVALID_SAMPLE_RATE;
  }
  return G2026Dsp_Analyze(
      &real_dsp_context,
      acquisition->channels[channel_id].samples,
      acquisition->sample_count,
      (uint32_t)lroundf(corrected_sample_rate_hz),
      false, false, &config, result);
}

#if G2026_HW_VARIANT == G2026_HW_VARIANT_AD9248_SINGLE_AD8421
#define G2026_AD8421_RECONSTRUCTION_POINTS 1024U
#define G2026_PI_F 3.14159265358979323846F

static float ReconstructAd8421InputVpp(const G2026DspResult *result)
{
  float minimum = INFINITY;
  float maximum = -INFINITY;

  for (uint32_t n = 0U; n < G2026_AD8421_RECONSTRUCTION_POINTS; ++n)
  {
    float value_mv = 0.0F;
    for (uint8_t i = 0U; i < result->component_count; ++i)
    {
      const G2026MeasuredComponent *component = &result->components[i];
      const float angle =
          2.0F * G2026_PI_F * (float)component->harmonic_order *
              (float)n / (float)G2026_AD8421_RECONSTRUCTION_POINTS +
          component->grid_phase_rad;
      value_mv += component->grid_peak_mv * cosf(angle);
    }
    minimum = fminf(minimum, value_mv);
    maximum = fmaxf(maximum, value_mv);
  }
  return maximum - minimum;
}

static bool CorrectAd8421Result(G2026DspResult *result,
                               float *fundamental_gain)
{
  float spectral_square_sum = 0.0F;

  if ((result == NULL) || (fundamental_gain == NULL) ||
      (result->component_count == 0U))
  {
    return false;
  }
  for (uint8_t i = 0U; i < result->component_count; ++i)
  {
    G2026MeasuredComponent *component = &result->components[i];
    float grid_direct_scale;
    float raw_direct_scale;
    float grid_gain;
    float raw_gain;
    if (!G2026Ad8421Calibration_Lookup(
            component->frequency_hz, &grid_direct_scale, &grid_gain) ||
        !G2026Ad8421Calibration_Lookup(
            component->raw_frequency_hz, &raw_direct_scale, &raw_gain))
    {
      return false;
    }
    if (i == 0U)
    {
      *fundamental_gain = grid_gain;
    }
    component->grid_peak_mv *=
        grid_direct_scale / grid_gain;
    component->peak_mv *=
        raw_direct_scale / raw_gain;
    spectral_square_sum +=
        component->grid_peak_mv * component->grid_peak_mv;
  }

  result->spectral_rms_mv = sqrtf(0.5F * spectral_square_sum);
  result->reconstructed_vpp_mv = ReconstructAd8421InputVpp(result);
  result->reconstructed_vpp_valid = true;

  /* Time/noise fields are diagnostics; use the fundamental correction. */
  float fundamental_direct_scale;
  float checked_gain;
  if (!G2026Ad8421Calibration_Lookup(
          result->components[0].frequency_hz,
          &fundamental_direct_scale, &checked_gain))
  {
    return false;
  }
  const float diagnostic_scale = fundamental_direct_scale / checked_gain;
  result->dc_mv *= diagnostic_scale;
  result->total_rms_mv *= diagnostic_scale;
  result->ac_rms_mv *= diagnostic_scale;
  result->time_vpp_mv *= diagnostic_scale;
  result->fft_noise_floor_rms_mv *= diagnostic_scale;
  result->residual_rms_mv *= diagnostic_scale;
  result->rms_difference_mv =
      fabsf(result->ac_rms_mv - result->spectral_rms_mv);
  return true;
}

static void PrintAd8421RawComponents(const G2026DspResult *result)
{
  for (uint8_t i = 0U; i < result->component_count; ++i)
  {
    const G2026MeasuredComponent *component = &result->components[i];
    const long frequency_x100 = lroundf(component->frequency_hz * 100.0F);
    const long code_vpp_x100 =
        lroundf(2.0F * component->grid_peak_mv * 100.0F);
    const int length = snprintf(
        debug_line, sizeof(debug_line),
        "[AD8421标定] %u次 f=%ld.%02ldHz A码值Vpp=%ld.%02ldcode\r\n",
        (unsigned int)(component->harmonic_order == 0U
                           ? 1U
                           : component->harmonic_order),
        frequency_x100 / 100L, labs(frequency_x100 % 100L),
        code_vpp_x100 / 100L, labs(code_vpp_x100 % 100L));
    Debug_Write(debug_line, length);
  }
}

static void ReportAd8421Single(const G2026FpgaAcquisition *acquisition,
                              bool signal_safe)
{
  G2026CalibrationRecord raw_code_calibration = {
      .input_mv_per_code = 1.0F,
      .zero_code = G2026_AD8421_ZERO_CODE_A,
      .sample_clock_ppm = 0.0F,
      .valid = true,
  };
  G2026DspResult result;

  if (!real_dsp_ready || !signal_safe)
  {
    Debug_Write("[异常] AD8421/A通道采样状态无效\r\n",
                sizeof("[异常] AD8421/A通道采样状态无效\r\n") - 1U);
    return;
  }

  const G2026DspStatus status = AnalyzeAcquisitionChannel(
      acquisition, 0U, &raw_code_calibration, &result);
  if (status != G2026_DSP_OK)
  {
    const int length = snprintf(debug_line, sizeof(debug_line),
                                "[异常] A通道信号识别失败 status=%u\r\n",
                                (unsigned int)status);
    Debug_Write(debug_line, length);
    return;
  }

  PrintAd8421RawComponents(&result);

  float fundamental_gain = 0.0F;
  if (!CorrectAd8421Result(&result, &fundamental_gain))
  {
    Debug_Write("[AD8421] 增益表未标定：当前仅输出原始码值，不输出电压\r\n",
                sizeof("[AD8421] 增益表未标定：当前仅输出原始码值，不输出电压\r\n") - 1U);
    return;
  }

  float fundamental_direct_scale;
  float checked_gain;
  if (!G2026Ad8421Calibration_Lookup(
          result.components[0].frequency_hz,
          &fundamental_direct_scale, &checked_gain))
  {
    return;
  }
  const float sample_scale = fundamental_direct_scale / fundamental_gain;
  for (uint16_t i = 0U; i < acquisition->sample_count; ++i)
  {
    real_dsp_context.calibrated_mv[i] =
        (real_dsp_context.calibrated_mv[i] - result.dc_mv / sample_scale) *
        sample_scale;
  }

  const uint32_t fundamental_hz = (uint32_t)lroundf(result.fundamental_hz);
  const float fundamental_vpp = 2.0F * result.components[0].grid_peak_mv;
  const long fundamental_vpp_x100 = lroundf(fundamental_vpp * 100.0F);
  const long spectral_rms_x100 = lroundf(result.spectral_rms_mv * 100.0F);
  const long reconstructed_vpp_x100 =
      lroundf(result.reconstructed_vpp_mv * 100.0F);
  const int length = snprintf(
      debug_line, sizeof(debug_line),
      "\r\n[测量结果] 基波=%lu.%01lukHz 基波Vpp=%ld.%02ldmV "
      "最终有效值=%ld.%02ldmV 总Vpp=%ld.%02ldmV 谐波数=%u\r\n",
      (unsigned long)(fundamental_hz / 1000U),
      (unsigned long)((fundamental_hz % 1000U) / 100U),
      fundamental_vpp_x100 / 100L, labs(fundamental_vpp_x100 % 100L),
      spectral_rms_x100 / 100L, labs(spectral_rms_x100 % 100L),
      reconstructed_vpp_x100 / 100L,
      labs(reconstructed_vpp_x100 % 100L),
      (unsigned int)(result.component_count - 1U));
  Debug_Write(debug_line, length);

  for (uint8_t i = 0U; i < result.component_count; ++i)
  {
    const G2026MeasuredComponent *component = &result.components[i];
    const long frequency_x10_khz =
        lroundf(component->frequency_hz / 100.0F);
    const long component_vpp_x100 =
        lroundf(2.0F * component->grid_peak_mv * 100.0F);
    const long raw_frequency_x100 =
        lroundf(component->raw_frequency_hz * 100.0F);
    const int component_length = snprintf(
        debug_line, sizeof(debug_line),
        "[分量] %u次 %ld.%01ldkHz Vpp=%ld.%02ldmV "
        "原始频率=%ld.%02ldHz\r\n",
        (unsigned int)(component->harmonic_order == 0U
                           ? 1U
                           : component->harmonic_order),
        frequency_x10_khz / 10L, labs(frequency_x10_khz % 10L),
        component_vpp_x100 / 100L, labs(component_vpp_x100 % 100L),
        raw_frequency_x100 / 100L, labs(raw_frequency_x100 % 100L));
    Debug_Write(debug_line, component_length);
  }

  float corrected_sample_rate_hz;
  if (G2026Calibration_CorrectSampleRate(
          &raw_code_calibration, acquisition->sample_rate_hz,
          &corrected_sample_rate_hz))
  {
    G2026Display_SubmitMeasurement(
        acquisition->acquisition_id,
        real_dsp_context.calibrated_mv,
        acquisition->sample_count,
        corrected_sample_rate_hz,
        &result, 1.0F);
  }
}
#endif

#if G2026_HW_VARIANT == G2026_HW_VARIANT_AD9248_SINGLE
#define G2026_DIRECT_RECONSTRUCTION_POINTS 1024U
#define G2026_DIRECT_PI_F 3.14159265358979323846F

static float ReconstructDirectVpp(const G2026DspResult *result)
{
  float minimum = INFINITY;
  float maximum = -INFINITY;

  for (uint32_t n = 0U; n < G2026_DIRECT_RECONSTRUCTION_POINTS; ++n)
  {
    float value_mv = 0.0F;
    for (uint8_t i = 0U; i < result->component_count; ++i)
    {
      const G2026MeasuredComponent *component = &result->components[i];
      const uint8_t harmonic_order =
          (i == 0U) ? 1U : component->harmonic_order;
      const float angle =
          2.0F * G2026_DIRECT_PI_F *
              (float)harmonic_order * (float)n /
              (float)G2026_DIRECT_RECONSTRUCTION_POINTS +
          component->grid_phase_rad;
      value_mv += component->grid_peak_mv * cosf(angle);
    }
    minimum = fminf(minimum, value_mv);
    maximum = fmaxf(maximum, value_mv);
  }
  return maximum - minimum;
}

static bool CorrectDirectResult(G2026DspResult *result)
{
  float spectral_square_sum = 0.0F;

  if ((result == NULL) || (result->component_count == 0U))
  {
    return false;
  }
  for (uint8_t i = 0U; i < result->component_count; ++i)
  {
    G2026MeasuredComponent *component = &result->components[i];
    float corrected_vpp_mv;
    if (!G2026Calibration_CorrectDirectVpp(
            component->frequency_hz,
            2.0F * component->grid_peak_mv,
            &corrected_vpp_mv))
    {
      return false;
    }
    component->grid_peak_mv = 0.5F * corrected_vpp_mv;
    spectral_square_sum +=
        component->grid_peak_mv * component->grid_peak_mv;
  }

  result->spectral_rms_mv = sqrtf(0.5F * spectral_square_sum);
  result->reconstructed_vpp_mv = ReconstructDirectVpp(result);
  result->reconstructed_vpp_valid = true;
  result->rms_difference_mv =
      fabsf(result->ac_rms_mv - result->spectral_rms_mv);
  return true;
}

/*
 * Stable, machine-readable records for the PC calibration controller.
 * All voltage fields are integer microvolts, frequencies are integer
 * centihertz, and SNR is centi-dB.  Keep human-facing Chinese logs intact.
 */
static void ReportDirectMachineRecords(const G2026FpgaAcquisition *acquisition,
                                       const G2026DspResult *result,
                                       float fundamental_vpp_mv,
                                       float final_rms_mv,
                                       float total_vpp_mv)
{
  const long fundamental_hz_x100 = lroundf(result->fundamental_hz * 100.0F);
  const long fundamental_vpp_uv = lroundf(fundamental_vpp_mv * 1000.0F);
  const long final_rms_uv = lroundf(final_rms_mv * 1000.0F);
  const long total_vpp_uv = lroundf(total_vpp_mv * 1000.0F);
  const long noise_floor_uv =
      lroundf(result->fft_noise_floor_rms_mv * 1000.0F);
  const long residual_uv = lroundf(result->residual_rms_mv * 1000.0F);
  const long snr_cdb = lroundf(result->fit_snr_db * 100.0F);
  const uint16_t flags = acquisition->channels[0].channel_flags;
  int length = snprintf(
      debug_line, sizeof(debug_line),
      "$M,acq=%lu,flags=%04X,f0_cHz=%ld,fund_vpp_uV=%ld,rms_uV=%ld,"
      "total_vpp_uV=%ld,noise_uV=%ld,residual_uV=%ld,snr_cdb=%ld,components=%u\r\n",
      (unsigned long)acquisition->acquisition_id, (unsigned int)flags,
      fundamental_hz_x100, fundamental_vpp_uv, final_rms_uv, total_vpp_uv,
      noise_floor_uv, residual_uv, snr_cdb,
      (unsigned int)result->component_count);
  Debug_Write(debug_line, length);

  for (uint8_t i = 0U; i < result->component_count; ++i)
  {
    const G2026MeasuredComponent *component = &result->components[i];
    const uint8_t order = (i == 0U) ? 1U : component->harmonic_order;
    const long grid_frequency_hz_x100 =
        lroundf(component->frequency_hz * 100.0F);
    const long raw_frequency_hz_x100 =
        lroundf(component->raw_frequency_hz * 100.0F);
    const long grid_vpk_uv = lroundf(
        component->grid_peak_mv * G2026_DIRECT_INPUT_CORRECTION * 1000.0F);
    const long grid_vpp_uv = 2L * grid_vpk_uv;
    length = snprintf(
        debug_line, sizeof(debug_line),
        "$C,acq=%lu,index=%u,order=%u,f_grid_cHz=%ld,f_raw_cHz=%ld,"
        "vpk_uV=%ld,vpp_uV=%ld\r\n",
        (unsigned long)acquisition->acquisition_id, (unsigned int)i,
        (unsigned int)order, grid_frequency_hz_x100, raw_frequency_hz_x100,
        grid_vpk_uv, grid_vpp_uv);
    Debug_Write(debug_line, length);
  }
}

static void ReportDirectSingle(const G2026FpgaAcquisition *acquisition,
                               const G2026CalibrationRecord *calibration,
                               bool signal_safe)
{
  G2026DspResult result;

  if (!real_dsp_ready || !signal_safe || (calibration == NULL))
  {
    Debug_Write("[异常] 直连INA单通道采样或标定无效\r\n",
                sizeof("[异常] 直连INA单通道采样或标定无效\r\n") - 1U);
    return;
  }

  const G2026DspStatus status = AnalyzeAcquisitionChannel(
      acquisition, 0U, calibration, &result);
  if (status != G2026_DSP_OK)
  {
    const int length = snprintf(debug_line, sizeof(debug_line),
                                "[异常] 直连INA信号识别失败 status=%u\r\n",
                                (unsigned int)status);
    Debug_Write(debug_line, length);
    return;
  }

  if (!CorrectDirectResult(&result))
  {
    Debug_Write("[异常] 直连INA幅频校正失败\r\n",
                sizeof("[异常] 直连INA幅频校正失败\r\n") - 1U);
    return;
  }

  const uint32_t fundamental_hz = (uint32_t)lroundf(result.fundamental_hz);
  const float fundamental_vpp =
      result.component_count > 0U
          ? 2.0F * result.components[0].grid_peak_mv *
                G2026_DIRECT_INPUT_CORRECTION
          : 0.0F;
  const float final_rms_mv =
      result.spectral_rms_mv * G2026_DIRECT_INPUT_CORRECTION;
  const float time_rms_mv =
      result.ac_rms_mv * G2026_DIRECT_INPUT_CORRECTION;
  const float total_vpp_mv =
      (result.reconstructed_vpp_valid ? result.reconstructed_vpp_mv
                                      : result.time_vpp_mv) *
      G2026_DIRECT_INPUT_CORRECTION;
  const long fundamental_vpp_x100 = lroundf(fundamental_vpp * 100.0F);
  const long final_rms_x100 = lroundf(final_rms_mv * 100.0F);
  const long time_rms_x100 = lroundf(time_rms_mv * 100.0F);
  const long total_vpp_x100 = lroundf(total_vpp_mv * 100.0F);
  int length = snprintf(
      debug_line, sizeof(debug_line),
      "\r\n[测量结果] 基波=%lu.%01lukHz 基波Vpp=%ld.%02ldmV "
      "最终有效值=%ld.%02ldmV 时域有效值=%ld.%02ldmV "
      "总Vpp=%ld.%02ldmV 谐波数=%u\r\n",
      (unsigned long)(fundamental_hz / 1000U),
      (unsigned long)((fundamental_hz % 1000U) / 100U),
      fundamental_vpp_x100 / 100L, labs(fundamental_vpp_x100 % 100L),
      final_rms_x100 / 100L, labs(final_rms_x100 % 100L),
      time_rms_x100 / 100L, labs(time_rms_x100 % 100L),
      total_vpp_x100 / 100L, labs(total_vpp_x100 % 100L),
      (unsigned int)(result.component_count > 0U
                         ? result.component_count - 1U
                         : 0U));
  Debug_Write(debug_line, length);

  const long noise_floor_x1000 =
      lroundf(result.fft_noise_floor_rms_mv *
              G2026_DIRECT_INPUT_CORRECTION * 1000.0F);
  const long noise_floor_dbc_x100 =
      lroundf(result.fft_noise_floor_dbc * 100.0F);
  const long residual_x1000 =
      lroundf(result.residual_rms_mv *
              G2026_DIRECT_INPUT_CORRECTION * 1000.0F);
  const long snr_x100 = lroundf(result.fit_snr_db * 100.0F);
  length = snprintf(
      debug_line, sizeof(debug_line),
      "[噪声诊断] FFT噪底=%ld.%03ldmVrms/bin (%ld.%02lddBc/bin) "
      "残差=%ld.%03ldmVrms SNR=%ld.%02lddB bins=%u\r\n",
      noise_floor_x1000 / 1000L, labs(noise_floor_x1000 % 1000L),
      noise_floor_dbc_x100 / 100L, labs(noise_floor_dbc_x100 % 100L),
      residual_x1000 / 1000L, labs(residual_x1000 % 1000L),
      snr_x100 / 100L, labs(snr_x100 % 100L),
      (unsigned int)result.fft_noise_bin_count);
  Debug_Write(debug_line, length);

  for (uint8_t i = 0U; i < result.component_count; ++i)
  {
    const G2026MeasuredComponent *component = &result.components[i];
    const uint32_t component_hz = (uint32_t)lroundf(component->frequency_hz);
    const long grid_vpp_x100 = lroundf(
        2.0F * component->grid_peak_mv * G2026_DIRECT_INPUT_CORRECTION *
        100.0F);
    const long raw_frequency_x100 =
        lroundf(component->raw_frequency_hz * 100.0F);
    const long raw_vpp_x100 = lroundf(
        2.0F * component->peak_mv * G2026_DIRECT_INPUT_CORRECTION * 100.0F);
    length = snprintf(
        debug_line, sizeof(debug_line),
        "[分量] %s%u次 网格=%lu.%01lukHz Vpp=%ld.%02ldmV | "
        "原始=%ld.%02ldHz Vpp=%ld.%02ldmV\r\n",
        i == 0U ? "基波 " : "",
        (unsigned int)(i == 0U ? 1U : component->harmonic_order),
        (unsigned long)(component_hz / 1000U),
        (unsigned long)((component_hz % 1000U) / 100U),
        grid_vpp_x100 / 100L, labs(grid_vpp_x100 % 100L),
        raw_frequency_x100 / 100L, labs(raw_frequency_x100 % 100L),
        raw_vpp_x100 / 100L, labs(raw_vpp_x100 % 100L));
    Debug_Write(debug_line, length);
  }

  ReportDirectMachineRecords(acquisition, &result, fundamental_vpp,
                             final_rms_mv, total_vpp_mv);

  float corrected_sample_rate_hz;
  if (G2026Calibration_CorrectSampleRate(
          calibration, acquisition->sample_rate_hz,
          &corrected_sample_rate_hz))
  {
    G2026Display_SubmitMeasurement(
        acquisition->acquisition_id,
        real_dsp_context.calibrated_mv,
        acquisition->sample_count,
        corrected_sample_rate_hz,
        &result,
        G2026_DIRECT_INPUT_CORRECTION);
  }
}
#endif

static void ReportCompletedAcquisition(void)
{
  const G2026FpgaAcquisition *acquisition =
      G2026FpgaWindowAssembler_Get(&real_assembler);
  const uint16_t fatal_flags =
      G2026_FPGA_CHANNEL_FLAG_OTR |
      G2026_FPGA_CHANNEL_FLAG_RAIL_HIT |
      G2026_FPGA_CHANNEL_FLAG_FILTER_SATURATED |
      G2026_FPGA_CHANNEL_FLAG_PLL_UNLOCKED |
      G2026_FPGA_CHANNEL_FLAG_FIR_SCHEDULE_OVERRUN |
      G2026_FPGA_CHANNEL_FLAG_IDENTITY_UNVERIFIED;
  G2026CalibrationRecord calibrations[G2026_FPGA_MAX_CHANNELS];
  bool channel_usable[G2026_FPGA_MAX_CHANNELS] = {false, false};
  bool channel_signal_safe[G2026_FPGA_MAX_CHANNELS] = {false, false};

#if (G2026_HW_VARIANT == G2026_HW_VARIANT_AD9248_SINGLE_AD8421) || \
    (G2026_HW_VARIANT == G2026_HW_VARIANT_AD9248_SINGLE)
  (void)calibrations;
#endif
#if G2026_HW_VARIANT == G2026_HW_VARIANT_AD9248_SINGLE_AD8421
  (void)channel_usable;
#endif

  if (acquisition == NULL)
  {
    return;
  }

  int length = snprintf(
      debug_line, sizeof(debug_line),
      "[fpga] WINDOW READY acq=%lu adc=%u channels=%u fs=%lu "
      "filter=%08lX n=%u\r\n",
      (unsigned long)acquisition->acquisition_id,
      (unsigned int)acquisition->adc_model,
      (unsigned int)acquisition->channel_count,
      (unsigned long)acquisition->sample_rate_hz,
      (unsigned long)acquisition->filter_version,
      (unsigned int)acquisition->sample_count);
  (void)length;

  for (uint8_t channel_id = 0U;
       channel_id < acquisition->channel_count;
       ++channel_id)
  {
    const G2026FpgaAssembledChannel *channel =
        &acquisition->channels[channel_id];
    int16_t minimum = INT16_MAX;
    int16_t maximum = INT16_MIN;
    int64_t sample_sum = 0;
    long mean_x100;
    G2026CalibrationRecord calibration;
    const G2026CalibrationKey key = {
        .adc_model = acquisition->adc_model,
        .channel_id = channel_id,
        .frontend_range_id = channel->frontend_range_id,
        .filter_version = acquisition->filter_version,
    };
    for (uint16_t i = 0U; i < acquisition->sample_count; ++i)
    {
      if (channel->samples[i] < minimum)
      {
        minimum = channel->samples[i];
      }
      if (channel->samples[i] > maximum)
      {
        maximum = channel->samples[i];
      }
      sample_sum += channel->samples[i];
    }
    const G2026CalibrationStatus calibration_status =
        G2026Calibration_Select(&calibration_store, &key, &calibration);
    mean_x100 = (long)((sample_sum * 100LL) /
                       (int64_t)acquisition->sample_count);
    length = snprintf(
        debug_line, sizeof(debug_line),
        "[fpga] ch=%u range=%u bits=%u min=%d max=%d mean=%ld.%02ld flags=%04X "
        "calibration=%s\r\n",
        (unsigned int)channel_id,
        (unsigned int)channel->frontend_range_id,
        (unsigned int)channel->effective_bits,
        (int)minimum,
        (int)maximum,
        mean_x100 / 100L,
        labs(mean_x100 % 100L),
        (unsigned int)channel->channel_flags,
        calibration_status == G2026_CALIBRATION_OK
            ? "READY"
            : "REQUIRED");
    (void)length;
#if (G2026_HW_VARIANT == G2026_HW_VARIANT_AD9248_SINGLE_AD8421) || \
    (G2026_HW_VARIANT == G2026_HW_VARIANT_AD9248_SINGLE)
    Debug_Write(debug_line, length);
#endif

    if ((calibration_status == G2026_CALIBRATION_OK) &&
        ((channel->channel_flags & fatal_flags) == 0U))
    {
      calibrations[channel_id] = calibration;
      channel_usable[channel_id] = true;
    }
    channel_signal_safe[channel_id] =
        (channel->channel_flags & fatal_flags) == 0U;
  }

#if G2026_HW_VARIANT == G2026_HW_VARIANT_AD9248_SINGLE_AD8421
  ReportAd8421Single(acquisition, channel_signal_safe[0]);
#elif G2026_HW_VARIANT == G2026_HW_VARIANT_AD9248_SINGLE
  ReportDirectSingle(acquisition,
                     channel_usable[0] ? &calibrations[0] : NULL,
                     channel_signal_safe[0]);
#else
  if ((acquisition->channel_count == 2U) && real_dsp_ready &&
      channel_signal_safe[0] && channel_signal_safe[1])
  {
    G2026DspResult input_result;
    G2026DspResult output_result;
    G2026DualGainResult gain_result;
    const bool calibrated_pair =
        channel_usable[0] && channel_usable[1];
    if (!calibrated_pair)
    {
      memset(calibrations, 0, sizeof(calibrations));
      for (uint8_t channel_id = 0U; channel_id < 2U; ++channel_id)
      {
        calibrations[channel_id].input_mv_per_code = 1.0F;
        calibrations[channel_id].zero_code = 0.0F;
        calibrations[channel_id].sample_clock_ppm = 0.0F;
        calibrations[channel_id].valid = true;
      }
      Debug_Write("[异常] 电压标定不可用\r\n",
                  sizeof("[异常] 电压标定不可用\r\n") - 1);
    }
    const G2026DspStatus output_status =
        AnalyzeAcquisitionChannel(
            acquisition, 1U, &calibrations[1], &output_result);
    /*
     * Analyze channel A last so real_dsp_context retains the calibrated
     * apparatus-input window until the display adapter has copied it.
     * The two analyses are otherwise independent.
     */
    const G2026DspStatus input_status =
        AnalyzeAcquisitionChannel(
            acquisition, 0U, &calibrations[0], &input_result);

    if ((input_status == G2026_DSP_OK) &&
        (output_status == G2026_DSP_OK))
    {
      const G2026DualGainStatus gain_status =
          G2026DualGain_Calculate(
              &input_result, &output_result,
              1.0F, 200.0F, &gain_result);
      const float input_vpp = input_result.reconstructed_vpp_valid
                                  ? input_result.reconstructed_vpp_mv
                                  : input_result.time_vpp_mv;
      const uint32_t fundamental_hz =
          (uint32_t)lroundf(input_result.fundamental_hz);
      const float fundamental_vpp =
          input_result.component_count > 0U
              ? 2.0F * input_result.components[0].grid_peak_mv *
                    G2026_INPUT_LOAD_CORRECTION
              : 0.0F;
      /*
       * The contest input model is a fundamental plus one or two sinusoidal
       * harmonics.  Use the RMS reconstructed from the accepted spectral
       * components as the final result; retain time-domain AC RMS as a
       * quality cross-check because it also contains noise/unidentified
       * energy.
       */
      const float final_rms_mv =
          input_result.spectral_rms_mv * G2026_INPUT_LOAD_CORRECTION;
      const float time_rms_mv =
          input_result.ac_rms_mv * G2026_INPUT_LOAD_CORRECTION;
      const long final_rms_x100 = lroundf(final_rms_mv * 100.0F);
      const long time_rms_x100 = lroundf(time_rms_mv * 100.0F);
      const long total_vpp_x100 = lroundf(
          input_vpp * G2026_INPUT_LOAD_CORRECTION * 100.0F);
      length = snprintf(debug_line, sizeof(debug_line),
                        "\r\n[测量结果] 基波=%lu.%01lukHz "
                        "基波Vpp=%ldmV 最终有效值=%ld.%02ldmV "
                        "时域有效值=%ld.%02ldmV "
                        "总Vpp=%ld.%02ldmV 谐波数=%u\r\n",
                        (unsigned long)(fundamental_hz / 1000U),
                        (unsigned long)((fundamental_hz % 1000U) / 100U),
                        lroundf(fundamental_vpp),
                        final_rms_x100 / 100L,
                        labs(final_rms_x100 % 100L),
                        time_rms_x100 / 100L,
                        labs(time_rms_x100 % 100L),
                        total_vpp_x100 / 100L,
                        labs(total_vpp_x100 % 100L),
                        (unsigned int)(input_result.component_count > 0U
                                           ? input_result.component_count - 1U
                                           : 0U));
      Debug_Write(debug_line, length);
      const long noise_floor_x1000 = lroundf(
          input_result.fft_noise_floor_rms_mv * 1000.0F);
      const long noise_floor_dbc_x100 = lroundf(
          input_result.fft_noise_floor_dbc * 100.0F);
      const long residual_x1000 = lroundf(
          input_result.residual_rms_mv * 1000.0F);
      const long snr_x100 = lroundf(input_result.fit_snr_db * 100.0F);
      length = snprintf(
          debug_line, sizeof(debug_line),
          "[噪声诊断] FFT噪底=%ld.%03ldmVrms/bin (%ld.%02lddBc/bin) "
          "残差=%ld.%03ldmVrms SNR=%ld.%02lddB bins=%u\r\n",
          noise_floor_x1000 / 1000L, labs(noise_floor_x1000 % 1000L),
          noise_floor_dbc_x100 / 100L,
          labs(noise_floor_dbc_x100 % 100L), residual_x1000 / 1000L,
          labs(residual_x1000 % 1000L), snr_x100 / 100L,
          labs(snr_x100 % 100L),
          (unsigned int)input_result.fft_noise_bin_count);
      Debug_Write(debug_line, length);
      if (input_result.component_count > 0U)
      {
        const G2026MeasuredComponent *component =
            &input_result.components[0];
        const long raw_fundamental_hz_x100 = lroundf(
            component->raw_frequency_hz * 100.0F);
        const long grid_fundamental_vpp_x100 = lroundf(
            2.0F * component->grid_peak_mv *
            G2026_INPUT_LOAD_CORRECTION * 100.0F);
        const long raw_fundamental_vpp_x100 = lroundf(
            2.0F * component->peak_mv *
            G2026_INPUT_LOAD_CORRECTION * 100.0F);
        length = snprintf(
            debug_line, sizeof(debug_line),
            "[分量] 基波 网格=%lu.%01lukHz Vpp=%ld.%02ldmV | "
            "原始=%ld.%02ldHz Vpp=%ld.%02ldmV\r\n",
            (unsigned long)(fundamental_hz / 1000U),
            (unsigned long)((fundamental_hz % 1000U) / 100U),
            grid_fundamental_vpp_x100 / 100L,
            labs(grid_fundamental_vpp_x100 % 100L),
            raw_fundamental_hz_x100 / 100L,
            labs(raw_fundamental_hz_x100 % 100L),
            raw_fundamental_vpp_x100 / 100L,
            labs(raw_fundamental_vpp_x100 % 100L));
        Debug_Write(debug_line, length);
      }
      for (uint8_t i = 1U; i < input_result.component_count; ++i)
      {
        const G2026MeasuredComponent *component =
            &input_result.components[i];
        const uint32_t component_hz =
            (uint32_t)lroundf(component->frequency_hz);
        const long grid_harmonic_vpp_x100 = lroundf(
            2.0F * component->grid_peak_mv *
            G2026_INPUT_LOAD_CORRECTION * 100.0F);
        const long raw_harmonic_hz_x100 = lroundf(
            component->raw_frequency_hz * 100.0F);
        const long raw_harmonic_vpp_x100 = lroundf(
            2.0F * component->peak_mv *
            G2026_INPUT_LOAD_CORRECTION * 100.0F);
        length = snprintf(debug_line, sizeof(debug_line),
                          "[分量] %u次 网格=%lu.%01lukHz Vpp=%ld.%02ldmV | "
                          "原始=%ld.%02ldHz Vpp=%ld.%02ldmV\r\n",
                          (unsigned int)(component->harmonic_order == 0U
                                             ? 1U
                                             : component->harmonic_order),
                          (unsigned long)(component_hz / 1000U),
                          (unsigned long)((component_hz % 1000U) / 100U),
                          grid_harmonic_vpp_x100 / 100L,
                          labs(grid_harmonic_vpp_x100 % 100L),
                          raw_harmonic_hz_x100 / 100L,
                          labs(raw_harmonic_hz_x100 % 100L),
                          raw_harmonic_vpp_x100 / 100L,
                          labs(raw_harmonic_vpp_x100 % 100L));
        Debug_Write(debug_line, length);
      }

      {
        float corrected_display_sample_rate_hz;
        if (G2026Calibration_CorrectSampleRate(
                &calibrations[0], acquisition->sample_rate_hz,
                &corrected_display_sample_rate_hz))
        {
          G2026Display_SubmitMeasurement(
              acquisition->acquisition_id,
              real_dsp_context.calibrated_mv,
              acquisition->sample_count,
              corrected_display_sample_rate_hz,
              &input_result,
              G2026_INPUT_LOAD_CORRECTION);
        }
      }

      if (gain_status == G2026_DUAL_GAIN_OK)
      {
        const long rms_gain_x1000 =
            lroundf(gain_result.rms_gain * 1000.0F);
        (void)rms_gain_x1000;
      }
      else
      {
        length = snprintf(
            debug_line, sizeof(debug_line),
            "[gain] rejected status=%u\r\n",
            (unsigned int)gain_status);
        Debug_Write(debug_line, length);
      }
    }
    else
    {
      length = snprintf(
          debug_line, sizeof(debug_line),
        "[异常] 信号识别失败 A=%u B=%u\r\n",
          (unsigned int)input_status,
          (unsigned int)output_status);
      Debug_Write(debug_line, length);
    }
  }
  else
  {
    Debug_Write(
        "[异常] 双通道采样状态无效\r\n",
        sizeof("[异常] 双通道采样状态无效\r\n") - 1);
  }
#endif

  G2026FpgaWindowAssembler_Reset(&real_assembler);
  acquisition_completed_waiting_ack = false;
}
#endif

void BringupApp_Init(void)
{
  G2026Display_Init(&huart1);

  const FpgaSpiAdapter adapter = {
      .spi = &hspi3,
      .cs_port = FPGA_CS_N_GPIO_Port,
      .cs_pin = FPGA_CS_N_Pin,
      .drdy_port = FPGA_DRDY_GPIO_Port,
      .drdy_pin = FPGA_DRDY_Pin,
  };

  FpgaLinkService_Init(&fpga_link, &adapter);
#if G2026_ENABLE_BOOT_MOCK_SELFTEST
  RunDspMockSelftest();
#else
  G2026FpgaWindowAssembler_Init(&real_assembler);
  Ad9280V05_Reset();
  G2026Calibration_Init(&calibration_store);
  if (!LoadMeasuredAd9248Calibration())
  {
    Debug_Write("[异常] 标定参数载入失败\r\n",
                sizeof("[异常] 标定参数载入失败\r\n") - 1);
  }
  real_dsp_ready =
      G2026Dsp_Init(&real_dsp_context) == G2026_DSP_OK;
  acquisition_completed_waiting_ack = false;
  ad9280_v05_completed_waiting_ack = false;
  Debug_Write("[fpga] SPI3 link service ready\r\n",
              sizeof("[fpga] SPI3 link service ready\r\n") - 1);
#endif
}

void BringupApp_Process(void)
{
  G2026Display_Process();

#if G2026_ENABLE_BOOT_MOCK_SELFTEST
  return;
#else
  FpgaProtocolPacket packet;
  FpgaLinkEvent event;

  FpgaLinkService_Process(&fpga_link, HAL_GetTick());
  event = FpgaLinkService_TakeEvent(&fpga_link, &packet);

  switch (event)
  {
    case FPGA_LINK_EVENT_PACKET:
      ++packet_count;
      if (packet.version == FPGA_PROTOCOL_G2026_VERSION)
      {
        if (!AcceptUnifiedChunk(&packet))
        {
          (void)FpgaLinkService_RejectPacket(
              &fpga_link, HAL_GetTick());
          break;
        }
      }
      else if ((packet.version ==
                FPGA_PROTOCOL_AD9280_BRINGUP_VERSION) &&
               (packet.kind == AD9280_V05_KIND))
      {
        if (!Ad9280V05_AcceptChunk(&packet))
        {
          Debug_Write("[fpga] REJECT v05/24 metadata\r\n",
                      sizeof("[fpga] REJECT v05/24 metadata\r\n") - 1);
          (void)FpgaLinkService_RejectPacket(
              &fpga_link, HAL_GetTick());
          break;
        }
      }
      else if ((packet_count <= 12U) || ((packet_count % 100U) == 0U))
      {
        ReportPacket(&packet);
      }
      if (!FpgaLinkService_AcceptPacket(
              &fpga_link, HAL_GetTick()))
      {
        Debug_Write("[fpga] ERROR ACK start failed\r\n",
                    sizeof("[fpga] ERROR ACK start failed\r\n") - 1);
      }
      break;
    case FPGA_LINK_EVENT_PACKET_ACKED:
      if (acquisition_completed_waiting_ack)
      {
        ReportCompletedAcquisition();
      }
      if (ad9280_v05_completed_waiting_ack)
      {
        Ad9280V05_ReportWindow();
      }
      break;
    case FPGA_LINK_EVENT_BAD_HEADER:
      ++bad_header_count;
      if ((bad_header_count <= 12U) || ((bad_header_count % 100U) == 0U))
      {
        ReportBadHeader();
      }
      break;
    case FPGA_LINK_EVENT_BAD_CRC:
      Debug_Write("[fpga] ERROR bad CRC; no ACK\r\n",
                  sizeof("[fpga] ERROR bad CRC; no ACK\r\n") - 1);
      break;
    case FPGA_LINK_EVENT_SPI_ERROR:
      Debug_Write("[fpga] ERROR SPI/DMA\r\n",
                  sizeof("[fpga] ERROR SPI/DMA\r\n") - 1);
      break;
    case FPGA_LINK_EVENT_TIMEOUT:
      Debug_Write("[fpga] ERROR transfer timeout; no ACK\r\n",
                  sizeof("[fpga] ERROR transfer timeout; no ACK\r\n") - 1);
      break;
    default:
      break;
  }
#endif
}

void BringupApp_OnDrdyIrq(uint16_t gpio_pin)
{
  if (gpio_pin == FPGA_DRDY_Pin)
  {
    FpgaLinkService_OnDrdyIrq(&fpga_link);
  }
}

void BringupApp_OnSpiComplete(SPI_HandleTypeDef *spi)
{
  FpgaLinkService_OnSpiComplete(&fpga_link, spi);
}

void BringupApp_OnSpiError(SPI_HandleTypeDef *spi)
{
  FpgaLinkService_OnSpiError(&fpga_link, spi);
}

void BringupApp_OnUartRxComplete(UART_HandleTypeDef *uart)
{
  G2026Display_OnUartRxComplete(uart);
}

void BringupApp_OnUartTxComplete(UART_HandleTypeDef *uart)
{
  G2026Display_OnUartTxComplete(uart);
}

void BringupApp_OnUartError(UART_HandleTypeDef *uart)
{
  G2026Display_OnUartError(uart);
}
