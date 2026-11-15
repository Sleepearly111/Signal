#include "selftest.h"
#include "classify.h"
#include "estimate.h"
#include "hmi.h"
#include "app_config.h"
#include <math.h>
#include <string.h>

#define PI2F 6.2831853f
#define ST_NENV  2500   /* 12.5ms @200k */
#define ST_NFREQ 1500   /* 30ms  @50k  */

static uint16_t st_env[ST_NENV];
static float    st_freq[ST_NFREQ];

static const char *tname(ModType_t t)
{
  static const char *n[] = {"?", "AM", "FM", "CW", "2ASK", "2FSK", "2PSK"};
  return n[t];
}

/* 0xB4D2 循环码图案 */
static int bit_at(int k) { return (0xB4D2 >> (k & 15)) & 1; }

static void fill_const(void)
{
  for (int i = 0; i < ST_NENV; i++)  st_env[i] = (uint16_t)(ENV_BIAS_ADC + 800);
  for (int i = 0; i < ST_NFREQ; i++) st_freq[i] = 2.0e6f;
}

static int check(const char *name, ModType_t got, ModType_t want,
                 int ok_params)
{
  int pass = (got == want) && ok_params;
  dbg_printf("[selftest] %-5s -> %-5s %s\r\n", name, tname(got),
             pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

static int t_am(void)
{
  fill_const();
  for (int i = 0; i < ST_NENV; i++)
    st_env[i] = (uint16_t)(ENV_BIAS_ADC + 800.0f *
                (1.0f + 0.5f * sinf(PI2F * 2000.0f * i / ENV_SAMPLE_RATE)));
  ModType_t t = Classify(st_env, ST_NENV, st_freq, ST_NFREQ);
  EstResult_t r;
  Estimate(t, st_env, ST_NENV, st_freq, ST_NFREQ, &r);
  dbg_printf("  ma=%.2f F=%.0f\r\n", r.ma, r.F_hz);
  return check("AM", t, MOD_AM,
               fabsf(r.ma - 0.5f) <= 0.1f && r.F_hz == 2000.0f);
}

static int t_fm(void)
{
  fill_const();
  for (int i = 0; i < ST_NFREQ; i++)
    st_freq[i] = 2.0e6f + 15000.0f * sinf(PI2F * 3000.0f * i / FREQ_SAMPLE_RATE);
  ModType_t t = Classify(st_env, ST_NENV, st_freq, ST_NFREQ);
  EstResult_t r;
  Estimate(t, st_env, ST_NENV, st_freq, ST_NFREQ, &r);
  dbg_printf("  mf=%.2f dF=%.0f F=%.0f\r\n", r.mf, r.dfmax, r.F_hz);
  return check("FM", t, MOD_FM,
               fabsf(r.dfmax - 15000.0f) <= 300.0f &&
               fabsf(r.mf - 5.0f) <= 0.3f && r.F_hz == 3000.0f);
}

static int t_cw(void)
{
  fill_const();
  ModType_t t = Classify(st_env, ST_NENV, st_freq, ST_NFREQ);
  return check("CW", t, MOD_CW, 1);
}

static int t_ask(void)
{
  fill_const();
  int spb = ENV_SAMPLE_RATE / 8000;   /* 25 样本/比特 @8kbps */
  for (int i = 0; i < ST_NENV; i++)
    st_env[i] = (uint16_t)(ENV_BIAS_ADC + (bit_at(i / spb) ? 900 : 100));
  ModType_t t = Classify(st_env, ST_NENV, st_freq, ST_NFREQ);
  EstResult_t r;
  Estimate(t, st_env, ST_NENV, st_freq, ST_NFREQ, &r);
  dbg_printf("  Rc=%d\r\n", r.Rc_bps);
  return check("2ASK", t, MOD_2ASK, r.Rc_bps == 8000);
}

static int t_fsk(void)
{
  fill_const();
  int spb = (int)(FREQ_SAMPLE_RATE / 10000.0f);   /* 5 样本/比特 @10kbps */
  for (int i = 0; i < ST_NFREQ; i++)
    st_freq[i] = 2.0e6f + (bit_at(i / spb) ? 12500.0f : -12500.0f);
  ModType_t t = Classify(st_env, ST_NENV, st_freq, ST_NFREQ);
  EstResult_t r;
  Estimate(t, st_env, ST_NENV, st_freq, ST_NFREQ, &r);
  dbg_printf("  Rc=%d h=%.2f\r\n", r.Rc_bps, r.h);
  return check("2FSK", t, MOD_2FSK,
               r.Rc_bps == 10000 && fabsf(r.h - 2.5f) <= 0.3f);
}

static int t_psk(void)
{
  fill_const();
  /* 6kbps: 比特边界在 freq 序列上间隔 50k/6k=8.33 样本, 相位翻转处放 1 个低频毛刺 */
  float spb = FREQ_SAMPLE_RATE / 6000.0f;
  int prev = bit_at(0);
  for (int k = 1; k * spb < ST_NFREQ; k++) {
    int b = bit_at(k);
    if (b != prev)
      st_freq[(int)(k * spb + 0.5f)] = 2.0e6f - 25000.0f;
    prev = b;
  }
  ModType_t t = Classify(st_env, ST_NENV, st_freq, ST_NFREQ);
  EstResult_t r;
  Estimate(t, st_env, ST_NENV, st_freq, ST_NFREQ, &r);
  dbg_printf("  Rc=%d\r\n", r.Rc_bps);
  return check("2PSK", t, MOD_2PSK, r.Rc_bps == 6000);
}

void SelfTest_Run(void)
{
  dbg_printf("== SELFTEST begin ==\r\n");
  int fail = 0;
  fail += t_am();
  fail += t_fm();
  fail += t_cw();
  fail += t_ask();
  fail += t_fsk();
  fail += t_psk();
  dbg_printf("== SELFTEST %s (%d fail) ==\r\n", fail ? "FAIL" : "PASS", fail);
}
