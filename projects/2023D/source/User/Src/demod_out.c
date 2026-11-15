#include "demod_out.h"
#include "app_config.h"
#include "dac.h"
#include "tim.h"
#include <math.h>

#define PI2F 6.2831853f
#define DEMOD_DAC_SAMPLES_PER_CYCLE 64
#define ASK_DAC_SAMPLES_PER_CYCLE   32

static uint16_t dac_buf[DAC_BUF_LEN];
static float scratch[FREQ_MAX_SAMPLES];   /* 2PSK 码序列重建用 */

/* 设置 TIM7 更新频率 = DAC 输出采样率 */
static void tim7_set_rate(float hz)
{
  uint32_t div = (uint32_t)(TIM7_CLK_HZ / hz + 0.5f);
  uint32_t psc = 0;
  while (div / (psc + 1) > 65536) psc++;
  __HAL_TIM_SET_PRESCALER(&htim7, psc);
  __HAL_TIM_SET_AUTORELOAD(&htim7, div / (psc + 1) - 1);
  __HAL_TIM_SET_COUNTER(&htim7, 0);
}

void DemodOut_Stop(void)
{
  HAL_DAC_Stop_DMA(&hdac, DAC_CHANNEL_1);
  HAL_TIM_Base_Stop(&htim7);
}

static void play(int n, float rate_hz)
{
  DemodOut_Stop();
  tim7_set_rate(rate_hz);
  HAL_DAC_Start_DMA(&hdac, DAC_CHANNEL_1, (uint32_t *)dac_buf, n, DAC_ALIGN_12B_R);
  HAL_TIM_Base_Start(&htim7);
}

void DemodOut_PlayTestSine(void)
{
  for (int i = 0; i < DAC_BUF_LEN; i++)
    dac_buf[i] = (uint16_t)(2047.5f + 2047.5f * sinf(PI2F * i / DAC_BUF_LEN));
  play(DAC_BUF_LEN, DAC_TEST_SINE_HZ * DAC_BUF_LEN); /* 一圈缓冲 = 1 个正弦周期 */
}

/* 把 src 前 n_use 点重采样进 dac_buf 并满幅缩放到 0..4095 */
static void fill_from_f32(const float *src, int n_use)
{
  float lo = src[0], hi = src[0];
  for (int i = 0; i < n_use; i++) {
    if (src[i] < lo) lo = src[i];
    if (src[i] > hi) hi = src[i];
  }
  float span = (hi - lo) > 1e-6f ? (hi - lo) : 1.0f;
  for (int i = 0; i < DAC_BUF_LEN; i++) {
    int j = (int)((float)i * n_use / DAC_BUF_LEN);
    dac_buf[i] = (uint16_t)(4095.0f * (src[j] - lo) / span);
  }
}

/* 2ASK真实包络先做5点平滑，再用施密特门限恢复二进制电平。 */
static void fill_ask_binary(const uint16_t *src, int n, float f_hz)
{
  float dc = 0.0f;
  float iq_cos = 0.0f, iq_sin = 0.0f;

  for (int i = 0; i < n; i++) dc += (float)src[i];
  dc /= (float)n;

  for (int i = 0; i < n; i++) {
    float w = PI2F * f_hz * (float)i / (float)ENV_SAMPLE_RATE;
    float x = (float)src[i] - dc;
    iq_cos += x * cosf(w);
    iq_sin += x * sinf(w);
  }

  float iq_mag = sqrtf(iq_cos * iq_cos + iq_sin * iq_sin);
  if (iq_mag < 1e-6f) {
    iq_cos = 1.0f;
    iq_sin = 0.0f;
    iq_mag = 1.0f;
  }

  for (int i = 0; i < DAC_BUF_LEN; i++) {
    float w = PI2F * (float)(i % ASK_DAC_SAMPLES_PER_CYCLE)
            / (float)ASK_DAC_SAMPLES_PER_CYCLE;
    float phase_signal = iq_cos * cosf(w) + iq_sin * sinf(w);
    dac_buf[i] = (phase_signal >= 0.0f) ? 3840U : 256U;
  }
}

/* 从真实包络在估计频率处提取I/Q，再重构约2Vpp的干净正弦。
   每周期64点，2048点缓冲中包含整数个周期，循环播放无接缝。 */
static void fill_am_sine_fit(const uint16_t *src, int n, float f_hz)
{
  const float out_amp_code = 1240.0f; /* 2*1240/4095*3.3V ≈ 2.0Vpp */
  float dc = 0.0f;
  float iq_cos = 0.0f, iq_sin = 0.0f;

  for (int i = 0; i < n; i++) dc += (float)src[i];
  dc /= (float)n;

  for (int i = 0; i < n; i++) {
    float w = PI2F * f_hz * (float)i / (float)ENV_SAMPLE_RATE;
    float x = (float)src[i] - dc;
    iq_cos += x * cosf(w);
    iq_sin += x * sinf(w);
  }

  float iq_mag = sqrtf(iq_cos * iq_cos + iq_sin * iq_sin);
  if (iq_mag < 1e-6f) {
    iq_cos = 1.0f;
    iq_sin = 0.0f;
    iq_mag = 1.0f;
  }

  for (int i = 0; i < DAC_BUF_LEN; i++) {
    float w = PI2F * (float)(i % DEMOD_DAC_SAMPLES_PER_CYCLE)
            / (float)DEMOD_DAC_SAMPLES_PER_CYCLE;
    float y = (iq_cos * cosf(w) + iq_sin * sinf(w)) / iq_mag;
    dac_buf[i] = (uint16_t)(2047.5f + out_amp_code * y);
  }
}

/* 从实际瞬时频率序列提取FM调制分量的I/Q并重构正弦。 */
static void fill_fm_sine_fit(const float *src, int n, float f_hz)
{
  const float out_amp_code = 1240.0f;
  float dc = 0.0f;
  float iq_cos = 0.0f, iq_sin = 0.0f;

  for (int i = 0; i < n; i++) dc += src[i];
  dc /= (float)n;

  for (int i = 0; i < n; i++) {
    float w = PI2F * f_hz * (float)i / FREQ_SAMPLE_RATE;
    float x = src[i] - dc;
    iq_cos += x * cosf(w);
    iq_sin += x * sinf(w);
  }

  float iq_mag = sqrtf(iq_cos * iq_cos + iq_sin * iq_sin);
  if (iq_mag < 1e-6f) {
    iq_cos = 1.0f;
    iq_sin = 0.0f;
    iq_mag = 1.0f;
  }

  for (int i = 0; i < DAC_BUF_LEN; i++) {
    float w = PI2F * (float)(i % DEMOD_DAC_SAMPLES_PER_CYCLE)
            / (float)DEMOD_DAC_SAMPLES_PER_CYCLE;
    float y = (iq_cos * cosf(w) + iq_sin * sinf(w)) / iq_mag;
    dac_buf[i] = (uint16_t)(2047.5f + out_amp_code * y);
  }
}

/* 从实际瞬时频率序列提取FSK切换相位，再恢复规则二电平。 */
static void fill_fsk_binary_fit(const float *src, int n, float rc_hz)
{
  float dc = 0.0f;
  float iq_cos = 0.0f, iq_sin = 0.0f;

  for (int i = 0; i < n; i++) dc += src[i];
  dc /= (float)n;

  for (int i = 0; i < n; i++) {
    float w = PI2F * rc_hz * (float)i / FREQ_SAMPLE_RATE;
    float x = src[i] - dc;
    iq_cos += x * cosf(w);
    iq_sin += x * sinf(w);
  }

  float iq_mag = sqrtf(iq_cos * iq_cos + iq_sin * iq_sin);
  if (iq_mag < 1e-6f) {
    iq_cos = 1.0f;
    iq_sin = 0.0f;
  }

  for (int i = 0; i < DAC_BUF_LEN; i++) {
    float w = PI2F * (float)(i % ASK_DAC_SAMPLES_PER_CYCLE)
            / (float)ASK_DAC_SAMPLES_PER_CYCLE;
    float phase_signal = iq_cos * cosf(w) + iq_sin * sinf(w);
    dac_buf[i] = (phase_signal >= 0.0f) ? 3840U : 256U;
  }
}

/* 截取整数个 period_hz 周期以减小循环回放的接缝 */
static int trim_periods(int n_src, float fs, float period_hz)
{
  if (period_hz <= 0) return n_src;
  int spp = (int)(fs / period_hz + 0.5f);
  if (spp <= 0 || spp > n_src) return n_src;
  return (n_src / spp) * spp;
}

void DemodOut_PlayDemod(ModType_t t, const uint16_t *env, int n_env,
                        const float *freq, int n_freq, const EstResult_t *r)
{
  int n_use;
  switch (t) {
  case MOD_AM:
    fill_am_sine_fit(env, n_env, r->F_hz);
    play(DAC_BUF_LEN, r->F_hz * (float)DEMOD_DAC_SAMPLES_PER_CYCLE);
    break;

  case MOD_2ASK:
    fill_ask_binary(env, n_env, (float)r->Rc_bps);
    play(DAC_BUF_LEN,
         (float)r->Rc_bps * (float)ASK_DAC_SAMPLES_PER_CYCLE);
    break;

  case MOD_FM:
    fill_fm_sine_fit(freq, n_freq, r->F_hz);
    play(DAC_BUF_LEN, r->F_hz * (float)DEMOD_DAC_SAMPLES_PER_CYCLE);
    break;

  case MOD_2FSK:
    fill_fsk_binary_fit(freq, n_freq, (float)r->Rc_bps);
    play(DAC_BUF_LEN,
         (float)r->Rc_bps * (float)ASK_DAC_SAMPLES_PER_CYCLE);
    break;

  case MOD_2PSK: {
    fill_fsk_binary_fit(freq, n_freq, (float)r->Rc_bps);
    play(DAC_BUF_LEN,
         (float)r->Rc_bps * (float)ASK_DAC_SAMPLES_PER_CYCLE);
    break;

    /* 相位跳变毛刺 → 翻转电平, 重建 NRZ 码序列波形 */
    float avg = 0;
    for (int i = 0; i < n_freq; i++) avg += freq[i];
    avg /= n_freq;
    float level = 0.0f;
    for (int i = 0; i < n_freq; i++) {
      if (fabsf(freq[i] - avg) > 3.0f * FREQ_DEV_TH_HZ) {
        level = 1.0f - level;
        int skip_end = i + GLITCH_RUN_MAX;
        if (skip_end >= n_freq) skip_end = n_freq - 1;
        for (; i <= skip_end; i++) scratch[i] = level;   /* 毛刺区间也写入新电平 */
        i = skip_end;   /* 补偿外层 i++, 使 skip_end+1 走正常路径写入 */
        continue;
      }
      scratch[i] = level;
    }
    n_use = trim_periods(n_freq, FREQ_SAMPLE_RATE, (float)r->Rc_bps);
    fill_from_f32(scratch, n_use);
    play(DAC_BUF_LEN, DAC_BUF_LEN * FREQ_SAMPLE_RATE / n_use);
    break;
  }

  default: /* CW / UNKNOWN: 输出中位直流 */
    for (int i = 0; i < DAC_BUF_LEN; i++) dac_buf[i] = 2048;
    play(DAC_BUF_LEN, 20000.0f);
    break;
  }
}
