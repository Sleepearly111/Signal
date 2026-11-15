#include "classify.h"
#include "stats.h"
#include "app_config.h"
#include <math.h>

static float env_tone_peak(const uint16_t *env, int n)
{
  static const float f_hz[F_CANDIDATE_NUM] = F_CANDIDATES_HZ;
  float dc = 0.0f;
  float peak = 0.0f;

  for (int i = 0; i < n; i++) dc += (float)env[i];
  dc /= (float)n;
  if (dc <= 1.0f) return 0.0f;

  for (int k = 0; k < F_CANDIDATE_NUM; k++) {
    float w = 6.2831853f * f_hz[k] / (float)ENV_SAMPLE_RATE;
    float c = 2.0f * cosf(w);
    float s1 = 0.0f, s2 = 0.0f;
    for (int i = 0; i < n; i++) {
      float s0 = ((float)env[i] - dc) + c * s1 - s2;
      s2 = s1;
      s1 = s0;
    }
    float re = s1 - s2 * cosf(w);
    float im = s2 * sinf(w);
    float mag = (2.0f * sqrtf(re * re + im * im) / (float)n) / dc;
    if (mag > peak) peak = mag;
  }
  return peak;
}

static float freq_tone_mag(const float *freq, int n, float tone_hz)
{
  float dc = 0.0f;
  for (int i = 0; i < n; i++) dc += freq[i];
  dc /= (float)n;

  float w = 6.2831853f * tone_hz / FREQ_SAMPLE_RATE;
  float c = 2.0f * cosf(w);
  float s1 = 0.0f, s2 = 0.0f;
  for (int i = 0; i < n; i++) {
    float s0 = (freq[i] - dc) + c * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  float re = s1 - s2 * cosf(w);
  float im = s2 * sinf(w);
  return 2.0f * sqrtf(re * re + im * im) / (float)n;
}

ModType_t Classify(const uint16_t *env, int n_env, const float *freq, int n_freq)
{
  /* --- 包络指标 --- */
  float e5  = Stats_EnvPercentile(env, n_env, 0.05f);
  float e95 = Stats_EnvPercentile(env, n_env, 0.95f);
  float ac_swing = e95 - e5;
  float ac_mean  = (e95 + e5) * 0.5f;
  float env_ripple = (ac_mean > 1.0f) ? ac_swing / ac_mean : 0.0f;

  /*
   * 高调制度AM在包络谷底可能使比较器暂时丢边沿，n_freq会下降到
   * 2ASK判据范围。因此先识别稳定的1~5kHz包络单音，再使用频率点数。
   */
  if (env_tone_peak(env, n_env) > ENV_TONE_TH)
    return MOD_AM;

  /* OOK低电平码段没有载波，有效瞬时频率点显著少于连续载波。 */
  if (n_freq >= FREQ_VALID_MIN_COUNT && n_freq < ASK_FREQ_MAX_COUNT)
    return MOD_2ASK;

  /* --- 包络有起伏 → AM或2ASK --- */
  if (env_ripple > ENV_RIPPLE_TH) {
    float frac = Stats_MidFractionU16(env, n_env, e5, e95);
    /* 2ASK低电平无过零→InstFreq过滤长间隔→n_freq远小于CW/AM */
    if (n_freq < FREQ_VALID_MIN_COUNT)
      return MOD_2ASK;
    return (frac > MID_FRACTION_TH) ? MOD_AM : MOD_2ASK;
  }

  /* --- 频率偏移游程分析 --- */
  float f50 = (n_freq > 0) ? Stats_FloatPercentile(freq, n_freq, 0.50f) : 0.0f;
  int n_glitch = 0, n_sustain = 0;
  int run = 0;
  float run_peak = 0;
  for (int i = 0; i <= n_freq; i++) {
    float d = (i < n_freq) ? fabsf(freq[i] - f50) : 0.0f;
    if (i < n_freq && d > FREQ_DEV_TH_HZ) {
      run++;
      if (d > run_peak) run_peak = d;
    } else if (run > 0) {
      if (run <= GLITCH_RUN_MAX && run_peak > 3.0f * FREQ_DEV_TH_HZ)
        n_glitch++;
      else if (run >= SUSTAIN_RUN)
        n_sustain++;
      run = 0;
      run_peak = 0;
    }
  }

  if (n_sustain >= 2) {
    static const float fm_cand[F_CANDIDATE_NUM] = F_CANDIDATES_HZ;
    static const int fsk_cand[RC_CANDIDATE_NUM] = RC_CANDIDATES_BPS;
    float fm_peak = 0.0f, fsk_peak = 0.0f;
    for (int i = 0; i < F_CANDIDATE_NUM; i++) {
      float mag = freq_tone_mag(freq, n_freq, fm_cand[i]);
      if (mag > fm_peak) fm_peak = mag;
    }
    for (int i = 0; i < RC_CANDIDATE_NUM; i++) {
      float mag = freq_tone_mag(freq, n_freq, (float)fsk_cand[i]);
      if (mag > fsk_peak) fsk_peak = mag;
    }
    return (fsk_peak > fm_peak) ? MOD_2FSK : MOD_FM;
  }
  if (n_glitch >= GLITCH_MIN_COUNT)
    return MOD_2PSK;
  return MOD_CW;
}
