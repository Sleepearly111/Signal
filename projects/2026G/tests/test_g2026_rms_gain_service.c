#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#include "g2026_rms_gain_service.h"

typedef struct
{
  uint8_t channel;
  bool enabled;
  unsigned disable_count;
  float input_mv;
  float output_mv;
} FakeBoard;

static bool fake_set_mux(void *context, uint8_t channel, bool enable)
{
  FakeBoard *board = context;
  board->channel = channel;
  board->enabled = enable;
  if (!enable)
  {
    board->disable_count++;
  }
  return true;
}

static G2026_RmsAdcStatus fake_read(void *context, float *millivolts)
{
  FakeBoard *board = context;
  if (!board->enabled)
  {
    return G2026_RMS_ADC_ERROR;
  }
  *millivolts = (board->channel == 0U)
                    ? board->input_mv
                    : board->output_mv;
  return G2026_RMS_ADC_READY;
}

static G2026_RmsGainConfig config(void)
{
  G2026_RmsGainConfig value = {
      .input_mux_channel = 0U,
      .output_mux_channel = 1U,
      .samples_per_path = 5U,
      .settle_ms = 10U,
      .sample_interval_ms = 2U,
      .total_timeout_ms = 100U,
      .minimum_input_rms_mv = 5.0f,
      .minimum_allowed_gain = 1.0f,
      .maximum_allowed_gain = 20.0f};
  return value;
}

static G2026_RmsGainResult run(FakeBoard *board,
                               G2026_RmsGainConfig *cfg)
{
  G2026_RmsGainAdapter adapter = {
      .context = board,
      .set_mux = fake_set_mux,
      .read_rms_mv = fake_read};
  G2026_RmsGainService service;
  G2026_RmsGainResult result;
  assert(G2026_RmsGainService_Init(&service, &adapter, cfg));
  assert(G2026_RmsGainService_Start(&service, 0U));
  for (uint32_t now = 0U; now < 100U; now++)
  {
    G2026_RmsGainService_Process(&service, now);
  }
  assert(G2026_RmsGainService_TakeResult(&service, &result));
  assert(!board->enabled);
  return result;
}

static void test_gain(void)
{
  FakeBoard board = {.input_mv = 20.0f, .output_mv = 100.0f};
  G2026_RmsGainConfig cfg = config();
  G2026_RmsGainResult result = run(&board, &cfg);
  assert(result.code == G2026_RMS_GAIN_RESULT_OK);
  assert(fabsf(result.measured_gain - 5.0f) < 0.0001f);
  assert(board.disable_count >= 3U);
}

static void test_small_input_rejected(void)
{
  FakeBoard board = {.input_mv = 2.0f, .output_mv = 20.0f};
  G2026_RmsGainConfig cfg = config();
  G2026_RmsGainResult result = run(&board, &cfg);
  assert(result.code == G2026_RMS_GAIN_RESULT_INVALID_INPUT);
}

static void test_gain_range_rejected(void)
{
  FakeBoard board = {.input_mv = 10.0f, .output_mv = 500.0f};
  G2026_RmsGainConfig cfg = config();
  G2026_RmsGainResult result = run(&board, &cfg);
  assert(result.code == G2026_RMS_GAIN_RESULT_OUT_OF_RANGE);
}

int main(void)
{
  test_gain();
  test_small_input_rejected();
  test_gain_range_rejected();
  puts("g2026 rms gain service tests passed");
  return 0;
}
