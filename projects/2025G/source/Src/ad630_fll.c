#include "ad630_fll.h"
#include "ADS8688.h"
#include "app_config.h"
#include "math.h"
#include <stdio.h>

/* =========================================================================
 * FLL v2 — continuous AD630 sampling + beat-frequency SEARCH
 * ========================================================================= */

/* Main-loop serviced; hardware logs are typically about 60 SPS. */
#define FLL_SAMPLE_INTERVAL_MS  2U
#define FLL_WINDOW_SAMPLES      100U   /* elapsed time is measured at runtime */
#define FLL_ZC_TIMEOUT_MS      20000U  /* measure beat down to about 0.05 Hz */

/* ── SEARCH: beat-frequency lock ── */
#define FLL_BEAT_LOCK_HZ        0.18f  /* fine lock with estimator-noise margin */
#define FLL_HOLD_UNLOCK_HZ      0.35f  /* hysteresis before returning to SEARCH */
#define FLL_BEAT_STABLE_WINDOWS 2U     /* fresh low-beat measurements required */
#define FLL_PP_MIN_MV           3.0f   /* min pp to confirm real beat (anti-alias) */
#define FLL_SEARCH_FINE_REVERSE_HZ   0.04f
#define FLL_SEARCH_COARSE_REVERSE_HZ 0.15f
#define FLL_SEARCH_COARSE_BEAT_HZ    0.50f
#define FLL_SEARCH_COARSE_STEP       5  /* 5 x 0.1 Hz executor increment */
#define FLL_SEARCH_FINE_STEP         1  /* 1 x 0.1 Hz executor increment */

/* Quiet-output fallback when no complete beat period is available. */
#define FLL_HOLD_STABLE_SLOPE_MV_S 20.0f

#define FLL_UNLOCK_CONFIRM_WINDOWS 2U

#define FLL_MEAN_EMA_ALPHA        0.05f   /* slow EMA for DC-level tracking */

/* ── Master switch ── */
#define AD630_FLL_ENABLE          1

/* =========================================================================
 * Measurement state — continuous sampling
 * ========================================================================= */
static float    s_sample_buf[FLL_WINDOW_SAMPLES];
static uint16_t s_sample_idx;           /* write cursor in sample_buf */
static uint32_t s_last_sample_ms;
static uint32_t s_window_start_ms;

/* Beat detection — continuous zero-crossing timing */
static float    s_dc_level;             /* EMA of AD630 output */
static uint32_t s_last_zc_rise_ms;      /* last rising-edge ZC time */
static float    s_beat_period_ms;       /* EMA of measured beat period */
static uint8_t  s_zc_armed;            /* hysteresis: must go below dc first */
static uint8_t  s_beat_update_pending; /* a new complete ZC period is ready */

/* Window statistics (computed after 100 samples) */
static float    s_win_mean_mv;
static float    s_win_pp_mv;
static float    s_win_ac_rms_mv;
static float    s_win_beat_hz;
static uint32_t s_win_zc_count;
static uint32_t s_win_zc_report;
static uint8_t  s_win_beat_fresh;
static uint32_t s_win_duration_ms;
static uint16_t s_win_sample_count;

/* =========================================================================
 * FLL state machine — unchanged architecture
 * ========================================================================= */
static uint8_t        s_fll_channel;
static uint8_t        s_fll_running;
static AD630_FLL_State s_fll_state;
static uint8_t        s_stable_count;
static int8_t         s_search_dir;
static uint8_t        s_search_have_reference;
static float          s_search_prev_beat_hz;

/* HOLD: filtered tracking */
static float          s_filtered_slope_mv_s;

/* Drift detection */

/* Polarity self-correction */

/* Hold validation */
static uint8_t        s_hold_valid;

/* Unlock detection */
static uint8_t        s_unlock_count;

/* Pending adjustment (±1 = direction for AdvReplay_AdjustFrequency) */
static int            s_pending_adjustment;
static uint8_t        s_adjustment_ready;

/* =========================================================================
 * Raw AD630 read — single ADS8688 manual CH2 sample.
 * Blocks ~600 µs.  After read, ADC is put back in AUTO_RST mode so the
 * main-loop auto-scan (CH0+CH1) continues working.
 * ========================================================================= */
static float read_ad630_mv(void)
{
    uint16_t code = ADS8688_ReadCH2();
    AUTO_RST_Mode();  /* restore auto-scan after manual read */
    return ADS8688_CodeToMilliVolt(code);
}

/* =========================================================================
 * Accumulate — call from main loop as fast as possible.
 * Samples ~500 SPS (every 2 ms).  Feeds window buffer and ZC detector.
 * ========================================================================= */
static void accumulate_sample(void)
{
    uint32_t now = HAL_GetTick();
    float mv;

    /* Throttle to ~500 SPS */
    if (now - s_last_sample_ms < FLL_SAMPLE_INTERVAL_MS) {
        return;
    }
    s_last_sample_ms = now;

    mv = read_ad630_mv();

    /* No new crossing for long enough: the previous beat period is stale. */
    if (s_last_zc_rise_ms > 0U &&
        now - s_last_zc_rise_ms > FLL_ZC_TIMEOUT_MS) {
        s_last_zc_rise_ms = 0U;
        s_beat_period_ms  = 0.0f;
        s_zc_armed        = 0U;
    }

    /* ── Window buffer ── */
    if (s_sample_idx < FLL_WINDOW_SAMPLES) {
        s_sample_buf[s_sample_idx] = mv;
        s_sample_idx++;
    }

    /* ── DC-level tracking (slow EMA) ── */
    if (s_dc_level == 0.0f) {
        s_dc_level = mv;
    } else {
        s_dc_level += FLL_MEAN_EMA_ALPHA * (mv - s_dc_level);
    }

    /* ── Zero-crossing detection for beat frequency ──
     * Hysteresis: arm when signal drops below dc by 2% of recent pp,
     * then detect rising edge through dc.  Each rising ZC = one beat cycle. */
    {
        float hysteresis = s_win_pp_mv * 0.05f;
        if (hysteresis < 0.5f) hysteresis = 0.5f;

        if (!s_zc_armed) {
            if (mv < s_dc_level - hysteresis) {
                s_zc_armed = 1U;
            }
        } else {
            if (mv >= s_dc_level) {
                /* Rising zero-crossing detected */
                s_win_zc_count++;
                if (s_last_zc_rise_ms > 0U) {
                    float period = (float)(now - s_last_zc_rise_ms);
                    if (s_beat_period_ms == 0.0f) {
                        s_beat_period_ms = period;
                    } else {
                        /* EMA: fast update for new measurements */
                        s_beat_period_ms += 0.3f *
                            (period - s_beat_period_ms);
                    }
                    s_beat_update_pending = 1U;
                }
                s_last_zc_rise_ms = now;
                s_zc_armed = 0U;
            }
        }
    }
}

/* =========================================================================
 * Process window after 100 samples.  Use measured elapsed time because the
 * cooperative main loop cannot guarantee the nominal 2 ms sample interval.
 * ========================================================================= */
static void process_window(void)
{
    uint16_t n = s_sample_idx;
    if (n < 10U) return;  /* need minimum samples */
    uint32_t now = HAL_GetTick();
    uint32_t elapsed_ms = now - s_window_start_ms;
    if (elapsed_ms == 0U) elapsed_ms = 1U;
    s_win_duration_ms = elapsed_ms;
    s_win_sample_count = n;
    s_win_beat_fresh = s_beat_update_pending;
    s_beat_update_pending = 0U;

    /* ── Statistics ── */
    float sum = 0.0f;
    float min_v = 1e9f, max_v = -1e9f;
    float sq_sum = 0.0f;
    uint16_t i;

    for (i = 0U; i < n; i++) {
        float v = s_sample_buf[i];
        sum += v;
        if (v < min_v) min_v = v;
        if (v > max_v) max_v = v;
        sq_sum += v * v;
    }

    s_win_mean_mv  = sum / (float)n;
    s_win_pp_mv    = max_v - min_v;
    {
        float rms     = sqrtf(sq_sum / (float)n);
        float ac_sq   = rms * rms - s_win_mean_mv * s_win_mean_mv;
        s_win_ac_rms_mv = (ac_sq > 0.0f) ? sqrtf(ac_sq) : 0.0f;
    }

    /* Beat frequency from ZC timing:
     * If we have valid ZC periods: beat_hz = 1000 / period_ms.
     * Fallback: each rising ZC is one beat cycle.                          */
    if (s_beat_period_ms > 10.0f) {
        s_win_beat_hz = 1000.0f / s_beat_period_ms;
    } else if (s_win_zc_count >= 2U) {
        s_win_beat_hz = (float)s_win_zc_count *
                         1000.0f / (float)s_win_duration_ms;
    } else {
        s_win_beat_hz = 0.0f;
    }

    /* ── Window slope (for HOLD use) ── */
    {
        float first_half_sum = 0.0f, second_half_sum = 0.0f;
        uint16_t half = n / 2U;
        for (i = 0U; i < half; i++) {
            first_half_sum  += s_sample_buf[i];
            second_half_sum += s_sample_buf[n - 1U - i];
        }
        float first_mean  = first_half_sum  / (float)half;
        float second_mean = second_half_sum / (float)half;
        s_filtered_slope_mv_s = (second_mean - first_mean) /
            ((float)s_win_duration_ms * 0.5f / 1000.0f);
    }

    /* Reset window */
    s_win_zc_report = s_win_zc_count;
    s_sample_idx   = 0U;
    s_win_zc_count = 0U;
    s_window_start_ms = now;
}

/* =========================================================================
 * Public API
 * ========================================================================= */
void AD630_FLL_Init(uint8_t ads8688_channel)
{
    s_fll_channel         = ads8688_channel;
    s_fll_running         = 1U;
    s_fll_state           = FLL_STATE_SEARCH;
    s_stable_count        = 0U;
    s_search_dir          = 1;
    s_search_have_reference = 0U;
    s_search_prev_beat_hz = 0.0f;
    s_filtered_slope_mv_s = 0.0f;
    s_hold_valid          = 0U;
    s_unlock_count        = 0U;
    s_pending_adjustment  = 0;
    s_adjustment_ready    = 0U;

    /* Measurement init */
    s_sample_idx      = 0U;
    s_last_sample_ms  = 0U;
    s_window_start_ms = HAL_GetTick();
    s_dc_level        = 0.0f;
    s_last_zc_rise_ms = 0U;
    s_beat_period_ms  = 0.0f;
    s_zc_armed        = 0U;
    s_beat_update_pending = 0U;
    s_win_mean_mv     = 0.0f;
    s_win_pp_mv       = 0.0f;
    s_win_ac_rms_mv   = 0.0f;
    s_win_beat_hz     = 0.0f;
    s_win_zc_count    = 0U;
    s_win_zc_report   = 0U;
    s_win_beat_fresh  = 0U;
    s_win_duration_ms = 0U;
    s_win_sample_count = 0U;

    printf("[AD630_FLL] Init on ADS8688 CH%u (v2: continuous sampling)\r\n",
           (unsigned int)ads8688_channel);
}

void AD630_FLL_Stop(void)
{
    s_fll_running = 0U;
    s_fll_state   = FLL_STATE_IDLE;
    s_hold_valid  = 0U;
    s_adjustment_ready = 0U;
    s_pending_adjustment = 0;
    printf("[AD630_FLL] Stopped\r\n");
}

AD630_FLL_State AD630_FLL_GetState(void) { return s_fll_state; }
uint8_t AD630_FLL_IsHoldValid(void)       { return s_hold_valid; }
int AD630_FLL_GetAdjustment(void)         { return s_pending_adjustment; }

void AD630_FLL_AdjustmentApplied(void)
{
    s_adjustment_ready   = 0U;
    s_pending_adjustment = 0;
}

void AD630_FLL_ResetBeatTracking(void)
{
    s_sample_idx       = 0U;
    s_window_start_ms  = HAL_GetTick();
    s_dc_level         = 0.0f;
    s_last_zc_rise_ms  = 0U;
    s_beat_period_ms   = 0.0f;
    s_zc_armed         = 0U;
    s_beat_update_pending = 0U;
    s_win_beat_hz      = 0.0f;
    s_win_zc_count     = 0U;
    s_win_zc_report    = 0U;
    s_win_beat_fresh   = 0U;
    s_win_duration_ms  = 0U;
    s_win_sample_count = 0U;
    s_stable_count     = 0U;
    s_search_dir       = 1;
    s_search_have_reference = 0U;
    s_search_prev_beat_hz = 0.0f;
}

/* =========================================================================
 * Public: accumulate a sample (call from main loop every iteration)
 * ========================================================================= */
void AD630_FLL_Accumulate(void)
{
    if (!s_fll_running) return;
#if AD630_FLL_ENABLE
    accumulate_sample();
#endif
}

/* =========================================================================
 * Main service — call from main loop.  Processes window when full.
 * Returns: 0=nothing, 1=freq adjustment pending, 2=state changed
 * ========================================================================= */
uint8_t AD630_FLL_Service(void)
{
    uint8_t ret = 0U;

    if (!s_fll_running) return 0U;

#if !AD630_FLL_ENABLE
    return 0U;
#endif

    /* Check if 200ms window is complete */
    if (s_sample_idx < FLL_WINDOW_SAMPLES) {
        return 0U;
    }

    process_window();

    /* =====================================================================
     * FLL_SEARCH — beat-frequency based lock detection
     * ===================================================================== */
    if (s_fll_state == FLL_STATE_SEARCH) {
        /* Anti-alias guard: if pp is very small but we're supposedly
         * in SEARCH, the beat might be aliased to DC at 5 Hz sampling.
         * Require meaningful pp to confirm real oscillation.             */
        uint8_t real_beat = (s_win_beat_fresh != 0U &&
                             s_win_pp_mv >= FLL_PP_MIN_MV &&
                             s_win_beat_hz > 0.05f);

        if (!real_beat) {
            /* No new complete beat period in this window.  Do not reuse an
             * old period for another adjustment.  Only accept truly quiet
             * output as a lock candidate. */
            uint8_t low_slope = (fabsf(s_filtered_slope_mv_s) <
                                 FLL_HOLD_STABLE_SLOPE_MV_S);
            if (low_slope && s_win_ac_rms_mv < 2.0f) {
                /* Very quiet — likely truly locked */
                if (s_stable_count < FLL_BEAT_STABLE_WINDOWS) {
                    s_stable_count++;
                }
            }
        } else if (s_win_beat_hz < FLL_BEAT_LOCK_HZ) {
            /* Beat low enough to consider lock */
            if (s_stable_count < FLL_BEAT_STABLE_WINDOWS) {
                s_stable_count++;
            }
        } else {
            /* Beat still present — keep searching */
            s_stable_count = 0U;

            /* Request frequency adjustment if not already pending */
            if (s_adjustment_ready == 0U) {
                float reverse_margin =
                    (s_win_beat_hz > FLL_SEARCH_COARSE_BEAT_HZ) ?
                    FLL_SEARCH_COARSE_REVERSE_HZ :
                    FLL_SEARCH_FINE_REVERSE_HZ;
                int search_step =
                    (s_win_beat_hz > FLL_SEARCH_COARSE_BEAT_HZ) ?
                    FLL_SEARCH_COARSE_STEP : FLL_SEARCH_FINE_STEP;

                /* Perturb-and-observe:
                 * the first step uses an arbitrary direction.  Every later
                 * valid window is the response to the previous step.
                 * Reverse only when the measured beat clearly worsens. */
                if (s_search_have_reference != 0U) {
                    if (s_win_beat_hz >
                        s_search_prev_beat_hz + reverse_margin) {
                        s_search_dir = (int8_t)-s_search_dir;
                    }
                } else {
                    s_search_have_reference = 1U;
                }
                s_search_prev_beat_hz = s_win_beat_hz;
                s_pending_adjustment =
                    (s_search_dir > 0) ? search_step : -search_step;
                s_adjustment_ready = 1U;
                ret = 1U;
            }
        }

        /* Transition to CALIBRATE */
        if (s_stable_count >= FLL_BEAT_STABLE_WINDOWS) {
            s_fll_state = FLL_STATE_CALIBRATE;
            ret = 2U;
        }

        printf("[AD630_FLL] SEARCH mean=%.1f pp=%.2f ac_rms=%.2f "
               "beat=%.3fHz zc=%lu n=%u win=%lums fs=%.1fSPS "
               "stable=%u/%u fresh=%u search_dir=%d ref=%.3f\r\n",
               (double)s_win_mean_mv, (double)s_win_pp_mv,
               (double)s_win_ac_rms_mv, (double)s_win_beat_hz,
               (unsigned long)s_win_zc_report,
               (unsigned int)s_win_sample_count,
               (unsigned long)s_win_duration_ms,
               (double)s_win_sample_count * 1000.0 /
                   (double)s_win_duration_ms,
               (unsigned int)s_stable_count,
               (unsigned int)FLL_BEAT_STABLE_WINDOWS,
               (unsigned int)s_win_beat_fresh,
               (int)s_search_dir,
               (double)s_search_prev_beat_hz);
        return ret;
    }

    /* =====================================================================
     * FLL_CALIBRATE — one-shot transition to HOLD
     * ===================================================================== */
    if (s_fll_state == FLL_STATE_CALIBRATE) {
        s_hold_valid          = 0U;
        s_unlock_count        = 0U;
        s_fll_state           = FLL_STATE_HOLD;

        printf("[AD630_FLL] CALIBRATE -> HOLD (DDS trim frozen)\r\n");
        return 2U;
    }

    /* =====================================================================
     * FLL_HOLD — frequency-only hold.
     *
     * AD630 phase continues to sweep whenever a small residual frequency
     * error remains, so its instantaneous slope is not an unlock criterion
     * for this FLL-only problem.  Freeze the best SEARCH trim and monitor
     * only newly measured complete beat periods.  Two fresh measurements
     * above the SEARCH lock threshold return to SEARCH.
     * ===================================================================== */
    if (s_fll_state == FLL_STATE_HOLD) {
        if (s_win_beat_fresh != 0U) {
            if (s_win_beat_hz > FLL_HOLD_UNLOCK_HZ) {
                if (s_unlock_count < FLL_UNLOCK_CONFIRM_WINDOWS) {
                    s_unlock_count++;
                }
            } else {
                s_unlock_count = 0U;
                s_hold_valid = 1U;
            }
        }

        if (s_unlock_count >= 2U) {
            s_hold_valid = 0U;
            s_fll_state = FLL_STATE_SEARCH;
            s_stable_count = 0U;
            s_unlock_count = 0U;
            s_search_dir = 1;
            s_search_have_reference = 0U;
            s_search_prev_beat_hz = 0.0f;
            printf("[AD630_FLL] HOLD beat high -> SEARCH\r\n");
            ret = 2U;
        }

        printf("[AD630_FLL] HOLD_FLL mean=%.1f pp=%.2f beat=%.3f "
               "fresh=%u unlock=%u/2 hold=%u trim=frozen\r\n",
               (double)s_win_mean_mv, (double)s_win_pp_mv,
               (double)s_win_beat_hz,
               (unsigned int)s_win_beat_fresh,
               (unsigned int)s_unlock_count,
               (unsigned int)s_hold_valid);
        return ret;
    }

    return ret;
}
