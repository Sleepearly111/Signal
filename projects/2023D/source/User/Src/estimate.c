#include "estimate.h"
#include "stats.h"
#include "app_config.h"
#include <math.h>
#include <string.h>

#define PI2F 6.2831853f
#define MAX_TRANS 512

static const float f_cand[F_CANDIDATE_NUM] = F_CANDIDATES_HZ;
static const int   rc_cand[RC_CANDIDATE_NUM] = RC_CANDIDATES_BPS;

/* ---- Goertzel: 序列在频率 f 处的正弦幅度估计 ---- */
static float goertzel_mag_f(const float *x, int n, float fs, float f)
{
  float dc = 0.0f;
  for (int i = 0; i < n; i++) dc += x[i];
  dc /= (float)n;

  float w = PI2F * f / fs, c = 2.0f * cosf(w);
  float s1 = 0, s2 = 0;
  for (int i = 0; i < n; i++) {
    float s0 = (x[i] - dc) + c * s1 - s2;
    s2 = s1; s1 = s0;
  }
  float re = s1 - s2 * cosf(w), im = s2 * sinf(w);
  return 2.0f * sqrtf(re * re + im * im) / n;
}

static float goertzel_mag_u16(const uint16_t *x, int n, float fs, float f)
{
  float w = PI2F * f / fs, c = 2.0f * cosf(w);
  float s1 = 0, s2 = 0;
  for (int i = 0; i < n; i++) {
    float s0 = (float)x[i] + c * s1 - s2;
    s2 = s1; s1 = s0;
  }
  float re = s1 - s2 * cosf(w), im = s2 * sinf(w);
  return 2.0f * sqrtf(re * re + im * im) / n;
}

/* ---- F 估计: 5 个候选频点 Goertzel 取最大 (先去直流) ---- */
static float best_F_env(const uint16_t *env, int n)
{
  /* 计算DC均值用于去直流 */
  float dc = 0;
  for (int i = 0; i < n; i++) dc += (float)env[i];
  dc /= n;

  float best = f_cand[0], bm = -1.0f;
  for (int i = 0; i < F_CANDIDATE_NUM; i++) {
    /* 用去直流后的float版本 */
    float w = PI2F * f_cand[i] / (float)ENV_SAMPLE_RATE;
    float c = 2.0f * cosf(w), s1 = 0, s2 = 0;
    for (int j = 0; j < n; j++) {
      float s0 = ((float)env[j] - dc) + c * s1 - s2;
      s2 = s1; s1 = s0;
    }
    float re = s1 - s2 * cosf(w), im = s2 * sinf(w);
    float m = 2.0f * sqrtf(re * re + im * im) / n;
    if (m > bm) { bm = m; best = f_cand[i]; }
  }
  return best;
}

static float best_F_freq(const float *freq, int n)
{
  float best = f_cand[0], bm = -1.0f;
  for (int i = 0; i < F_CANDIDATE_NUM; i++) {
    float m = goertzel_mag_f(freq, n, FREQ_SAMPLE_RATE, f_cand[i]);
    if (m > bm) { bm = m; best = f_cand[i]; }
  }
  return best;
}

/* ---- 带回差的二值化跳变位置提取 ---- */
static int transitions_u16(const uint16_t *x, int n, float lo, float hi,
                           int *tr, int max_tr)
{
  float lo_th = lo + 0.35f * (hi - lo), hi_th = lo + 0.65f * (hi - lo);
  int state = x[0] > hi_th, m = 0;
  for (int i = 1; i < n; i++) {
    if (!state && x[i] > hi_th) { state = 1; if (m < max_tr) tr[m++] = i; }
    else if (state && x[i] < lo_th) { state = 0; if (m < max_tr) tr[m++] = i; }
  }
  return m;
}

static int transitions_f(const float *x, int n, float lo, float hi,
                         int *tr, int max_tr)
{
  float lo_th = lo + 0.35f * (hi - lo), hi_th = lo + 0.65f * (hi - lo);
  int state = x[0] > hi_th, m = 0;
  for (int i = 1; i < n; i++) {
    if (!state && x[i] > hi_th) { state = 1; if (m < max_tr) tr[m++] = i; }
    else if (state && x[i] < lo_th) { state = 0; if (m < max_tr) tr[m++] = i; }
  }
  return m;
}

/* ---- 2PSK: 孤立毛刺中心位置 ---- */
static int glitch_positions(const float *x, int n, float f50, int *tr, int max_tr)
{
  int m = 0;
  for (int i = 0; i < n; i++) {
    if (fabsf(x[i] - f50) > 3.0f * FREQ_DEV_TH_HZ) {
      if (m < max_tr) tr[m++] = i;
      i += GLITCH_RUN_MAX;   /* 跳过同一毛刺 */
    }
  }
  return m;
}

/* ---- Rc: 跳变间隔对 3 个候选码率打分, 取余差最小者 ---- */
static int best_rc(const int *tr, int n_tr, float fs)
{
  int best = rc_cand[0];
  float bs = 1e30f;
  for (int c = 0; c < RC_CANDIDATE_NUM; c++) {
    float P = fs / (float)rc_cand[c];  /* 每比特样本数 */
    float s = 0;
    for (int i = 1; i < n_tr; i++) {
      float m = (float)(tr[i] - tr[i - 1]) / P;
      s += fabsf(m - roundf(m));
    }
    if (n_tr > 1) s /= (n_tr - 1);
    if (s < bs) { bs = s; best = rc_cand[c]; }
  }
  return best;
}

/* AFG1062内部FSK为周期切换，直接比较6/8/10kHz三个码率候选谱峰。 */
static int best_rc_freq_tone(const float *freq, int n)
{
  int best = rc_cand[0];
  float best_mag = -1.0f;
  for (int i = 0; i < RC_CANDIDATE_NUM; i++) {
    float mag = goertzel_mag_f(freq, n, FREQ_SAMPLE_RATE,
                               (float)rc_cand[i]);
    if (mag > best_mag) {
      best_mag = mag;
      best = rc_cand[i];
    }
  }
  return best;
}

void Estimate(ModType_t t, const uint16_t *env, int n_env,
              const float *freq, int n_freq, EstResult_t *out)
{
  static int tr[MAX_TRANS];
  memset(out, 0, sizeof(*out));
  out->type = t;

  switch (t) {
  case MOD_AM: {
    float e5  = Stats_EnvPercentile(env, n_env, 0.05f);
    float e95 = Stats_EnvPercentile(env, n_env, 0.95f);
    float e50 = Stats_EnvPercentile(env, n_env, 0.50f);
    float raw = (e95 - e5) / e50;
    /*
     * 2026-07-23 当前硬件实测分段标定：
     * raw 0.0198/0.0211/0.0258/0.0553/0.0666/0.0687
     * ma  0.30  /0.40  /0.60  /0.70  /0.80  /0.90
     * 0.60~0.70间包络输出存在明显跳变，保留该区间的实测分段。
     */
    float ma_cal;
    if (raw <= 0.0198f)
      ma_cal = raw * (0.30f / 0.0198f);
    else if (raw <= 0.0211f)
      ma_cal = 0.30f + (raw - 0.0198f) * (0.10f / 0.0013f);
    else if (raw <= 0.0258f)
      ma_cal = 0.40f + (raw - 0.0211f) * (0.20f / 0.0047f);
    else if (raw <= 0.0553f)
      ma_cal = 0.60f + (raw - 0.0258f) * (0.10f / 0.0295f);
    else if (raw <= 0.0666f)
      ma_cal = 0.70f + (raw - 0.0553f) * (0.10f / 0.0113f);
    else
      ma_cal = 0.80f + (raw - 0.0666f) * (0.10f / 0.0021f);
    if (ma_cal < 0.0f) ma_cal = 0.0f;
    if (ma_cal > 1.0f) ma_cal = 1.0f;
    out->ma   = ma_cal;
    out->F_hz = best_F_env(env, n_env);
    break;
  }
  case MOD_FM: {
    out->F_hz  = best_F_freq(freq, n_freq);
    out->dfmax = goertzel_mag_f(freq, n_freq,
                                FREQ_SAMPLE_RATE, out->F_hz);
    out->mf    = out->dfmax / out->F_hz;
    break;
  }
  case MOD_2ASK: {
    float e5  = Stats_EnvPercentile(env, n_env, 0.05f);
    float e95 = Stats_EnvPercentile(env, n_env, 0.95f);
    int m = transitions_u16(env, n_env, e5, e95, tr, MAX_TRANS);
    out->Rc_bps = best_rc(tr, m, (float)ENV_SAMPLE_RATE);
    break;
  }
  case MOD_2FSK: {
    float f5  = Stats_FloatPercentile(freq, n_freq, 0.05f);
    float f95 = Stats_FloatPercentile(freq, n_freq, 0.95f);
    out->Rc_bps = best_rc_freq_tone(freq, n_freq);
    out->h = (f95 - f5) / (float)out->Rc_bps;
    break;
  }
  case MOD_2PSK: {
    out->Rc_bps = best_rc_freq_tone(freq, n_freq);
    break;
  }
  default:
    break;
  }
}
