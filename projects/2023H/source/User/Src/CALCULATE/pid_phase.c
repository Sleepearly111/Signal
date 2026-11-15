#include "pid_phase.h"
#include "math.h"
#include "usart.h"
#include "ADS8688.h"
#include "stdlib.h"
#include "AD9833.h"
#include "calculate.h"
#include <stdio.h>
#include <string.h>

/* =========================================================================
 * FLL-only configuration — 比赛模式不使用相位锁定环
 * ========================================================================= */
#define find_range                  9
#define find_times                  3
#define PLL_PHASE_CH_NUM            2U
#define PLL_PHASE_FREQ_WORD_LIMIT   128
#define PLL_PHASE_DITHER_DEN        16

/* ---- Lock detection (SEARCH → LOCK_CALIBRATE) ---- */
#define FLL_LOCK_RMS_TH_MV          10.0f   /* 当前软件初值，待示波器硬件标定 */
#define FLL_LOCK_SLOPE_TH_MV_S      100.0f  /* 当前软件初值，待示波器硬件标定 */
#define FLL_LOCK_STABLE_NEED        3U

/* ---- Drift detection (FLL_HOLD) ---- */
#define FLL_DRIFT_SLOPE_TH_MV_S     35.0f   /* 当前软件初值，待示波器硬件标定 */
#define FLL_DRIFT_CONTROL_TH_MV      8.0f   /* 当前软件初值，待示波器硬件标定 */
#define FLL_LOOKAHEAD_S              0.25f  /* 当前软件初值，待示波器硬件标定 */
#define FLL_CONFIRM_NEED             3U
#define FLL_COOLDOWN_WINDOWS         2U

/* ---- Step sizes ---- */
#define FLL_SEARCH_STEP_UNITS        4      /* SEARCH 阶段使用较大步长 */
#define FLL_HOLD_STEP_UNITS          1      /* HOLD 阶段使用最小步长；无效则改 2 */

/* ---- Target tracking (比赛默认关闭) ---- */
#define FLL_ENABLE_TARGET_TRACKING   0

#define FLL_TARGET_TRACK_ALPHA       0.02f
#define FLL_TARGET_TRACK_ERR_MV      20.0f
#define FLL_TARGET_TRACK_SLOPE_MV_S  20.0f

/* ---- Hold validation ---- */
#define FLL_HOLD_CONFIRM_WINDOWS     5      /* 连续无动作窗数，初值待硬件验证 */
#define FLL_HOLD_STABLE_SLOPE_MV_S   20.0f

/* ---- Unlock detection (迟滞：阈值 > 锁定阈值) ---- */
#define FLL_UNLOCK_SLOPE_TH_MV_S     150.0f
#define FLL_UNLOCK_PREDICTED_TH_MV   30.0f
#define FLL_UNLOCK_CONFIRM_WINDOWS   3U

/* ---- Metric filter ---- */
#define FLL_METRIC_FILTER_ALPHA      0.35f

double judge1 = 0;
double judge2 = 0;

extern uint32_t Basic_Freq[2];
extern uint16_t Wave_Type[2];

/* ---- FLL state machine ---- */
typedef enum {
    FLL_IDLE = 0,
    FLL_SEARCH,
    FLL_LOCK_CALIBRATE,
    FLL_HOLD
} fll_state_t;

static const float pll_sign[PLL_PHASE_CH_NUM] = {1.0f, 1.0f};
static const int   pll_search_dir[PLL_PHASE_CH_NUM] = {1, 1};

static fll_state_t fll_state[PLL_PHASE_CH_NUM] = {FLL_IDLE, FLL_IDLE};
static uint8_t     stable_count[PLL_PHASE_CH_NUM] = {0U, 0U};
static float       v_target_mv[PLL_PHASE_CH_NUM] = {0.0f, 0.0f};
static float       filtered_mean_mv[PLL_PHASE_CH_NUM] = {0.0f, 0.0f};
static float       filtered_slope_mv_s[PLL_PHASE_CH_NUM] = {0.0f, 0.0f};
static uint8_t     filter_ready[PLL_PHASE_CH_NUM] = {0U, 0U};

/* ---- Drift detection state ---- */
static int8_t  drift_dir_last[PLL_PHASE_CH_NUM] = {0, 0};
static uint8_t drift_confirm_count[PLL_PHASE_CH_NUM] = {0U, 0U};
static uint8_t drift_cooldown[PLL_PHASE_CH_NUM] = {0U, 0U};

/* ---- Control polarity self-correction ---- */
static int8_t  fll_correction_polarity[PLL_PHASE_CH_NUM] = {1, 1};
static uint8_t correction_bad_count[PLL_PHASE_CH_NUM] = {0U, 0U};
static float   pre_action_abs_predicted[PLL_PHASE_CH_NUM];
static float   pre_action_abs_slope[PLL_PHASE_CH_NUM];
static uint8_t fll_action_pending[PLL_PHASE_CH_NUM] = {0U, 0U};

/* ---- Hold validation ---- */
static uint8_t hold_valid[PLL_PHASE_CH_NUM] = {0U, 0U};
static uint8_t hold_confirm_count[PLL_PHASE_CH_NUM] = {0U, 0U};

/* ---- Unlock detection ---- */
static uint8_t unlock_count[PLL_PHASE_CH_NUM] = {0U, 0U};

/* ---- LOCK_CALIBRATE saved values ---- */
static float lock_mean_mv[PLL_PHASE_CH_NUM];
static float lock_slope_mv_s[PLL_PHASE_CH_NUM];

static uint16_t base_delay_flag[PLL_PHASE_CH_NUM] = {0U, 0U};
static int      freq_word_offset[PLL_PHASE_CH_NUM] = {0, 0};
static uint32_t base_freq_word[4] = {0U, 0U, 0U, 0U};

static int PLLPhase_ClampDelay(int delay)
{
    if (delay < 0) {
        return 0;
    }
    if (delay > 16) {
        return 16;
    }
    return delay;
}

static uint32_t PLLPhase_AddWordOffset(uint32_t base_word, int offset)
{
    if ((offset < 0) && ((uint32_t)(-offset) > base_word)) {
        return 0U;
    }
    if ((offset > 0) && ((uint32_t)offset > (0xFFFFFFFFUL - base_word))) {
        return 0xFFFFFFFFUL;
    }
    if (offset < 0) {
        return base_word - (uint32_t)(-offset);
    }

    return base_word + (uint32_t)offset;
}

static int PLLPhase_ClampAverageUnits(int units)
{
    const int min_units = -PLL_PHASE_FREQ_WORD_LIMIT * PLL_PHASE_DITHER_DEN;
    const int max_units = PLL_PHASE_FREQ_WORD_LIMIT * PLL_PHASE_DITHER_DEN;

    if (units < min_units) {
        return min_units;
    }
    if (units > max_units) {
        return max_units;
    }

    return units;
}

static int PLLPhase_GetAverageUnits(uint8_t ch, const uint16_t delay_flag[2])
{
    int delay;

    delay = PLLPhase_ClampDelay((int)delay_flag[ch]);
    return (freq_word_offset[ch] * PLL_PHASE_DITHER_DEN) + (PLL_PHASE_DITHER_DEN - delay);
}

static void PLLPhase_SetAverageUnits(uint8_t ch, int units, uint16_t delay_flag[2])
{
    int offset;
    int high_slots;

    units = PLLPhase_ClampAverageUnits(units);
    if (units >= 0) {
        offset = units / PLL_PHASE_DITHER_DEN;
    } else {
        offset = -(((-units) + PLL_PHASE_DITHER_DEN - 1) / PLL_PHASE_DITHER_DEN);
    }

    high_slots = units - (offset * PLL_PHASE_DITHER_DEN);
    freq_word_offset[ch] = offset;
    delay_flag[ch] = (uint16_t)(PLL_PHASE_DITHER_DEN - high_slots);
}

/* (kept for reference; FLL_ApplyFrequencyStep replaces this) */
#if 0
static uint8_t PLLPhase_ApplyDelayStep(uint8_t ch,
                                       int step,
                                       uint16_t delay_flag[2],
                                       uint32_t real_freq_word[4])
{
    int units;
    int next_units;

    if ((ch >= PLL_PHASE_CH_NUM) || (delay_flag == 0) || (real_freq_word == 0) || (step == 0)) {
        return 0U;
    }

    units = PLLPhase_GetAverageUnits(ch, delay_flag);
    next_units = units + ((step > 0) ? -FLL_SEARCH_STEP_UNITS : FLL_SEARCH_STEP_UNITS);
    next_units = PLLPhase_ClampAverageUnits(next_units);
    if (next_units == units) {
        return 0U;
    }

    PLLPhase_SetAverageUnits(ch, next_units, delay_flag);
    return 1U;
}
#endif

/* ---- FLL frequency step with configurable step size ---- */
static uint8_t FLL_ApplyFrequencyStep(uint8_t ch,
                                       int direction,
                                       int step_units,
                                       uint16_t delay_flag[2],
                                       uint32_t real_freq_word[4])
{
    int units;
    int next_units;

    if ((ch >= PLL_PHASE_CH_NUM) || (delay_flag == 0)
        || (real_freq_word == 0) || (direction == 0)) {
        return 0U;
    }

    units = PLLPhase_GetAverageUnits(ch, delay_flag);
    next_units = units + ((direction > 0) ? -step_units : step_units);
    next_units = PLLPhase_ClampAverageUnits(next_units);
    if (next_units == units) {
        return 0U;
    }

    PLLPhase_SetAverageUnits(ch, next_units, delay_flag);
    return 1U;
}

static void PLLPhase_ApplyFreqWordOffset(uint8_t ch, uint32_t real_freq_word[4])
{
    uint8_t high_idx;

    if ((ch >= PLL_PHASE_CH_NUM) || (real_freq_word == 0)) {
        return;
    }

    high_idx = ch + 2U;
    real_freq_word[ch] = PLLPhase_AddWordOffset(base_freq_word[ch], freq_word_offset[ch]);
    real_freq_word[high_idx] = PLLPhase_AddWordOffset(base_freq_word[high_idx], freq_word_offset[ch]);
}

void PLLPhase_ResetRuntime(uint16_t delay_flag[2], uint32_t real_freq_word[4])
{
    uint8_t ch;

    if ((delay_flag == 0) || (real_freq_word == 0)) {
        return;
    }

    for (ch = 0U; ch < PLL_PHASE_CH_NUM; ch++) {
        fll_state[ch] = FLL_SEARCH;
        stable_count[ch] = 0U;
        v_target_mv[ch] = 0.0f;
        filtered_mean_mv[ch] = 0.0f;
        filtered_slope_mv_s[ch] = 0.0f;
        filter_ready[ch] = 0U;
        drift_dir_last[ch] = 0;
        drift_confirm_count[ch] = 0U;
        drift_cooldown[ch] = 0U;
        fll_correction_polarity[ch] = 1;
        correction_bad_count[ch] = 0U;
        pre_action_abs_predicted[ch] = 0.0f;
        pre_action_abs_slope[ch] = 0.0f;
        fll_action_pending[ch] = 0U;
        hold_valid[ch] = 0U;
        hold_confirm_count[ch] = 0U;
        unlock_count[ch] = 0U;
        lock_mean_mv[ch] = 0.0f;
        lock_slope_mv_s[ch] = 0.0f;
        base_delay_flag[ch] = (uint16_t)PLLPhase_ClampDelay((int)delay_flag[ch]);
        delay_flag[ch] = base_delay_flag[ch];
        freq_word_offset[ch] = 0;
    }

    base_freq_word[0] = real_freq_word[0];
    base_freq_word[1] = real_freq_word[1];
    base_freq_word[2] = real_freq_word[2];
    base_freq_word[3] = real_freq_word[3];
}

uint8_t PLLPhase_UpdateWindow(uint8_t ch,
                              const AD630LockMetric *metric,
                              uint16_t delay_flag[2],
                              uint32_t real_freq_word[4])
{
    float err_mv;
    float slope_mv_s;
    float predicted_err_mv = 0.0f;
    int   step = 0;
    int   candidate_dir = 0;
    uint8_t stable_now;
    uint8_t changed = 0U;
    const char *state_name;

    if ((ch >= PLL_PHASE_CH_NUM) || (metric == 0) || (delay_flag == 0) || (real_freq_word == 0)) {
        return 0U;
    }

    /* =================================================================
     * FLL state machine
     *
     * FLL_IDLE          → (PLLPhase_ResetRuntime sets FLL_SEARCH)
     * FLL_SEARCH        → FLL_LOCK_CALIBRATE (RMS+slope stable ×3)
     * FLL_LOCK_CALIBRATE → FLL_HOLD (one-shot, next window)
     * FLL_HOLD           → FLL_SEARCH (unlock detected)
     *
     * 不执行相位扫描、不写 PHASE 寄存器、不追求 AD630 输出最大值。
     * ================================================================= */

    switch (fll_state[ch]) {

    /* ---- FLL_SEARCH: 粗锁，快速趋近 ---- */
    case FLL_SEARCH:
        stable_now = ((metric->ac_rms_mv < FLL_LOCK_RMS_TH_MV) &&
                      (fabsf(metric->slope_mv_s) < FLL_LOCK_SLOPE_TH_MV_S)) ? 1U : 0U;
        if (stable_now != 0U) {
            if (stable_count[ch] < FLL_LOCK_STABLE_NEED) {
                stable_count[ch]++;
            }
            if (stable_count[ch] >= FLL_LOCK_STABLE_NEED) {
                /* Lock detected → calibrate on next window */
                filtered_mean_mv[ch]    = metric->mean_mv;
                filtered_slope_mv_s[ch] = metric->slope_mv_s;
                filter_ready[ch]        = 1U;
                fll_state[ch]           = FLL_LOCK_CALIBRATE;
            }
        } else {
            stable_count[ch] = 0U;
            step = pll_search_dir[ch];
            (void)FLL_ApplyFrequencyStep(ch, step, FLL_SEARCH_STEP_UNITS,
                                         delay_flag, real_freq_word);
        }

        PLLPhase_ApplyFreqWordOffset(ch, real_freq_word);
        printf("FLL ch=%d state=SEARCH mean=%.3f rms=%.3f slope=%.1f "
               "delay=%u offset=%d dir=%d stable=%u\r\n",
               ch, (double)metric->mean_mv, (double)metric->ac_rms_mv,
               (double)metric->slope_mv_s,
               (unsigned int)delay_flag[ch], freq_word_offset[ch],
               step, (unsigned int)stable_count[ch]);
        return (fll_state[ch] >= FLL_LOCK_CALIBRATE) ? 1U : 0U;

    /* ---- FLL_LOCK_CALIBRATE: 单次标定 target，不扫描相位 ---- */
    case FLL_LOCK_CALIBRATE:
        /* First filtered window after lock — use it to calibrate */
        if (filter_ready[ch] != 0U) {
            filtered_mean_mv[ch] += FLL_METRIC_FILTER_ALPHA
                * (metric->mean_mv - filtered_mean_mv[ch]);
            filtered_slope_mv_s[ch] += FLL_METRIC_FILTER_ALPHA
                * (metric->slope_mv_s - filtered_slope_mv_s[ch]);
        }

        /* One-shot calibration */
        v_target_mv[ch]       = filtered_mean_mv[ch];
        lock_mean_mv[ch]      = filtered_mean_mv[ch];
        lock_slope_mv_s[ch]   = filtered_slope_mv_s[ch];

        /* Clear all drift/control state */
        drift_dir_last[ch]        = 0;
        drift_confirm_count[ch]   = 0U;
        drift_cooldown[ch]        = 0U;
        fll_correction_polarity[ch] = 1;
        correction_bad_count[ch]  = 0U;
        fll_action_pending[ch]    = 0U;
        hold_valid[ch]            = 0U;
        hold_confirm_count[ch]    = 0U;
        unlock_count[ch]          = 0U;

        fll_state[ch] = FLL_HOLD;

        PLLPhase_ApplyFreqWordOffset(ch, real_freq_word);
        printf("FLL ch=%d state=CALIBRATE target=%.3f mV  → HOLD\r\n",
               ch, (double)v_target_mv[ch]);
        return 1U;

    /* ---- FLL_HOLD: 缓慢维护，最小步长，确认+冷却+极性自纠错 ---- */
    case FLL_HOLD: {
        uint8_t did_action = 0U;

        /* Filter measurements */
        filtered_mean_mv[ch] += FLL_METRIC_FILTER_ALPHA
            * (metric->mean_mv - filtered_mean_mv[ch]);
        filtered_slope_mv_s[ch] += FLL_METRIC_FILTER_ALPHA
            * (metric->slope_mv_s - filtered_slope_mv_s[ch]);

        err_mv    = (filtered_mean_mv[ch] - v_target_mv[ch]) * pll_sign[ch];
        slope_mv_s = filtered_slope_mv_s[ch] * pll_sign[ch];

        /* Target tracking — 比赛默认关闭 */
#if FLL_ENABLE_TARGET_TRACKING
        if ((fabsf(slope_mv_s) < FLL_TARGET_TRACK_SLOPE_MV_S) &&
            (fabsf(err_mv)    < FLL_TARGET_TRACK_ERR_MV)) {
            v_target_mv[ch] += FLL_TARGET_TRACK_ALPHA
                * (filtered_mean_mv[ch] - v_target_mv[ch]);
            err_mv = (filtered_mean_mv[ch] - v_target_mv[ch]) * pll_sign[ch];
        }
#endif

        /* Cooldown tick */
        if (drift_cooldown[ch] > 0U) {
            drift_cooldown[ch]--;
            /* Post-action evaluation when cooldown ends */
            if (drift_cooldown[ch] == 0U && fll_action_pending[ch] != 0U) {
                float post_predicted = fabsf(predicted_err_mv);
                float post_slope     = fabsf(slope_mv_s);
                uint8_t improved = ((post_predicted < pre_action_abs_predicted[ch])
                                    || (post_slope < pre_action_abs_slope[ch])) ? 1U : 0U;
                printf("[fll] effect ch=%d predicted_after=%.1f slope_after=%.1f "
                       "improved=%u\r\n",
                       ch, (double)post_predicted, (double)post_slope,
                       (unsigned int)improved);

                if (!improved) {
                    correction_bad_count[ch]++;
                    if (correction_bad_count[ch] >= 2U) {
                        fll_correction_polarity[ch] =
                            -fll_correction_polarity[ch];
                        correction_bad_count[ch] = 0U;
                        drift_confirm_count[ch]  = 0U;
                        printf("[fll] ch=%d polarity flipped to %d\r\n",
                               ch, (int)fll_correction_polarity[ch]);
                    }
                } else {
                    correction_bad_count[ch] = 0U;
                }
                fll_action_pending[ch] = 0U;
            }
        }

        /* Drift detection with lookahead prediction */
        predicted_err_mv = err_mv + (slope_mv_s * FLL_LOOKAHEAD_S);

        candidate_dir = 0;
        if ((fabsf(slope_mv_s) >= FLL_DRIFT_SLOPE_TH_MV_S) &&
            (fabsf(predicted_err_mv) >= FLL_DRIFT_CONTROL_TH_MV) &&
            ((fabsf(err_mv) < FLL_DRIFT_CONTROL_TH_MV) ||
             ((err_mv * slope_mv_s) >= 0.0f))) {
            candidate_dir = (predicted_err_mv > 0.0f) ? 1 : -1;
            candidate_dir *= (int)fll_correction_polarity[ch];
        }

        /* Direction confirmation */
        if (candidate_dir != 0) {
            if (drift_dir_last[ch] == candidate_dir) {
                if (drift_confirm_count[ch] < FLL_CONFIRM_NEED) {
                    drift_confirm_count[ch]++;
                }
            } else {
                drift_dir_last[ch]      = (int8_t)candidate_dir;
                drift_confirm_count[ch] = 1U;
            }

            if ((drift_confirm_count[ch] >= FLL_CONFIRM_NEED)
                && (drift_cooldown[ch] == 0U)) {

                /* Save pre-action metrics for effect evaluation */
                pre_action_abs_predicted[ch] = fabsf(predicted_err_mv);
                pre_action_abs_slope[ch]     = fabsf(slope_mv_s);

                step = candidate_dir;
                changed = FLL_ApplyFrequencyStep(ch, step, FLL_HOLD_STEP_UNITS,
                                                  delay_flag, real_freq_word);
                if (changed != 0U) {
                    drift_cooldown[ch]     = FLL_COOLDOWN_WINDOWS;
                    fll_action_pending[ch] = 1U;
                    did_action = 1U;
                    printf("[fll] action ch=%d dir=%d polarity=%d step=%d "
                           "predicted_before=%.1f slope_before=%.1f\r\n",
                           ch, step, (int)fll_correction_polarity[ch],
                           FLL_HOLD_STEP_UNITS,
                           (double)pre_action_abs_predicted[ch],
                           (double)pre_action_abs_slope[ch]);
                }
                drift_confirm_count[ch] = 0U;
            }
        } else if (drift_confirm_count[ch] > 0U) {
            drift_confirm_count[ch]--;
        }

        /* Hold valid confirmation */
        if (fabsf(slope_mv_s) < FLL_HOLD_STABLE_SLOPE_MV_S &&
            fabsf(predicted_err_mv) < FLL_DRIFT_CONTROL_TH_MV &&
            drift_cooldown[ch] == 0U) {
            if (hold_confirm_count[ch] < FLL_HOLD_CONFIRM_WINDOWS)
                hold_confirm_count[ch]++;
        } else if (did_action) {
            hold_confirm_count[ch] = 0U;
        }
        if (hold_confirm_count[ch] >= FLL_HOLD_CONFIRM_WINDOWS) {
            hold_valid[ch] = 1U;
        }

        /* Unlock detection (迟滞，阈值 > 锁定阈值) */
        {
            uint8_t unlocked = 0U;
            if (fabsf(slope_mv_s) > FLL_UNLOCK_SLOPE_TH_MV_S &&
                fabsf(predicted_err_mv) > FLL_UNLOCK_PREDICTED_TH_MV) {
                unlock_count[ch]++;
                if (unlock_count[ch] >= FLL_UNLOCK_CONFIRM_WINDOWS) {
                    unlocked = 1U;
                }
            } else {
                if (unlock_count[ch] > 0U) unlock_count[ch]--;
            }

            /* freq_word_offset 持续饱和也视为失锁 */
            if (freq_word_offset[ch] <= -(int)PLL_PHASE_FREQ_WORD_LIMIT ||
                freq_word_offset[ch] >=  (int)PLL_PHASE_FREQ_WORD_LIMIT) {
                unlock_count[ch]++;
                if (unlock_count[ch] >= FLL_UNLOCK_CONFIRM_WINDOWS) {
                    unlocked = 1U;
                }
            }

            if (unlocked) {
                hold_valid[ch]  = 0U;
                fll_state[ch]   = FLL_SEARCH;
                stable_count[ch] = 0U;
                unlock_count[ch] = 0U;
                printf("FLL ch=%d UNLOCKED → SEARCH\r\n", ch);
                /* fall through to SEARCH on next window */
            }
        }

        PLLPhase_ApplyFreqWordOffset(ch, real_freq_word);

        /* Debug output */
        switch (fll_state[ch]) {
            case FLL_SEARCH: state_name = "SEARCH"; break;
            case FLL_LOCK_CALIBRATE: state_name = "CALIBRATE"; break;
            case FLL_HOLD:  state_name = "HOLD";  break;
            default:        state_name = "IDLE";   break;
        }
        printf("FLL ch=%d state=%s mean=%.1f slope=%.1f target=%.1f "
               "err=%.1f pred=%.1f dir=%d pol=%d vote=%u cd=%u "
               "step=%d offset=%d delay=%u "
               "hold_ok=%u hold_cnt=%u\r\n",
               ch, state_name,
               (double)filtered_mean_mv[ch],
               (double)filtered_slope_mv_s[ch],
               (double)v_target_mv[ch],
               (double)err_mv,
               (double)predicted_err_mv,
               candidate_dir,
               (int)fll_correction_polarity[ch],
               (unsigned int)drift_confirm_count[ch],
               (unsigned int)drift_cooldown[ch],
               step,
               freq_word_offset[ch],
               (unsigned int)delay_flag[ch],
               (unsigned int)hold_valid[ch],
               (unsigned int)hold_confirm_count[ch]);

        return (fll_state[ch] >= FLL_LOCK_CALIBRATE) ? 1U : 0U;
    }

    default: /* FLL_IDLE — wait for external trigger */
        PLLPhase_ApplyFreqWordOffset(ch, real_freq_word);
        return 0U;
    }
}

uint8_t PLLPhase_IsLocked(uint8_t ch)
{
    if (ch >= PLL_PHASE_CH_NUM) {
        return 0U;
    }

    return (fll_state[ch] >= FLL_LOCK_CALIBRATE) ? 1U : 0U;
}

uint8_t FLL_IsHoldValid(uint8_t ch)
{
    if (ch >= PLL_PHASE_CH_NUM) {
        return 0U;
    }

    return hold_valid[ch];
}

void PLLPhase_Reset(const float lock_mv[2], const int initial_delay_flag[2], const uint32_t initial_freq_word[4])
{
    uint8_t ch;
    uint16_t runtime_delay_flag[PLL_PHASE_CH_NUM];

    (void)lock_mv;
    if ((initial_delay_flag == 0) || (initial_freq_word == 0)) {
        return;
    }

    for (ch = 0U; ch < PLL_PHASE_CH_NUM; ch++) {
        runtime_delay_flag[ch] = (uint16_t)PLLPhase_ClampDelay(initial_delay_flag[ch]);
    }

    PLLPhase_ResetRuntime(runtime_delay_flag, (uint32_t *)initial_freq_word);
}

void PLLPhase_Update(uint8_t ch, uint16_t ads_code, int delay_flag[2], uint32_t real_freq_word[4])
{
    AD630LockMetric metric;
    uint16_t runtime_delay_flag[PLL_PHASE_CH_NUM];
    uint8_t i;

    if ((ch >= PLL_PHASE_CH_NUM) || (delay_flag == 0) || (real_freq_word == 0)) {
        return;
    }

    for (i = 0U; i < PLL_PHASE_CH_NUM; i++) {
        runtime_delay_flag[i] = (uint16_t)PLLPhase_ClampDelay(delay_flag[i]);
    }

    metric.mean_mv = ADS8688_CodeToMilliVolt(ads_code);
    metric.ac_rms_mv = 0.0f;
    metric.min_mv = metric.mean_mv;
    metric.max_mv = metric.mean_mv;
    metric.pp_mv = 0.0f;
    metric.slope_mv_s = 0.0f;

    (void)PLLPhase_UpdateWindow(ch, &metric, runtime_delay_flag, real_freq_word);

    for (i = 0U; i < PLL_PHASE_CH_NUM; i++) {
        delay_flag[i] = (int)runtime_delay_flag[i];
    }
}

uint8_t AD630_MeasureWindowServiced(AD630LockMetric metric[2],
                                    uint16_t sample_count,
                                    uint16_t interval_ms,
                                    AD630WindowService service,
                                    void *context)
{
    uint16_t sample_idx;
    uint8_t ch;
    uint16_t code[PLL_PHASE_CH_NUM];
    uint8_t ready;
    float mv[PLL_PHASE_CH_NUM];
    float first_mv[PLL_PHASE_CH_NUM] = {0.0f, 0.0f};
    float last_mv[PLL_PHASE_CH_NUM] = {0.0f, 0.0f};
    double sum_mv[PLL_PHASE_CH_NUM] = {0.0, 0.0};
    double sum_sq_mv[PLL_PHASE_CH_NUM] = {0.0, 0.0};

    if ((metric == 0) || (sample_count == 0U) || (ads8688_fault != ADS8688_OK)) {
        return 0U;
    }

    for (ch = 0U; ch < PLL_PHASE_CH_NUM; ch++) {
        metric[ch].mean_mv = 0.0f;
        metric[ch].ac_rms_mv = 0.0f;
        metric[ch].min_mv = 0.0f;
        metric[ch].max_mv = 0.0f;
        metric[ch].pp_mv = 0.0f;
        metric[ch].slope_mv_s = 0.0f;
    }

    for (sample_idx = 0U; sample_idx < sample_count; sample_idx++) {
        if (service != 0) {
            service(context);
        }

        if (interval_ms > 0U) {
            HAL_Delay(interval_ms);
        }

        __disable_irq();
        ads8688_data_ready = 0U;
        ads8688_sample_request = 1U;
        __enable_irq();

        ADS8688_Service();

        __disable_irq();
        ready = ads8688_data_ready;
        code[0] = v_suoxiang[0];
        code[1] = v_suoxiang[1];
        ads8688_data_ready = 0U;
        __enable_irq();

        if ((ready == 0U) || (ads8688_fault != ADS8688_OK)) {
            return 0U;
        }

        for (ch = 0U; ch < PLL_PHASE_CH_NUM; ch++) {
            mv[ch] = ADS8688_CodeToMilliVolt(code[ch]);
            if (sample_idx == 0U) {
                first_mv[ch] = mv[ch];
                metric[ch].min_mv = mv[ch];
                metric[ch].max_mv = mv[ch];
            }
            if (mv[ch] < metric[ch].min_mv) {
                metric[ch].min_mv = mv[ch];
            }
            if (mv[ch] > metric[ch].max_mv) {
                metric[ch].max_mv = mv[ch];
            }
            last_mv[ch] = mv[ch];
            sum_mv[ch] += (double)mv[ch];
            sum_sq_mv[ch] += (double)mv[ch] * (double)mv[ch];
        }
    }

    for (ch = 0U; ch < PLL_PHASE_CH_NUM; ch++) {
        double mean = sum_mv[ch] / (double)sample_count;
        double variance = (sum_sq_mv[ch] / (double)sample_count) - (mean * mean);
        float total_time_s;

        if (variance < 0.0) {
            variance = 0.0;
        }

        metric[ch].mean_mv = (float)mean;
        metric[ch].ac_rms_mv = (float)sqrt(variance);
        metric[ch].pp_mv = metric[ch].max_mv - metric[ch].min_mv;
        if ((sample_count > 1U) && (interval_ms > 0U)) {
            total_time_s = ((float)(sample_count - 1U) * (float)interval_ms) / 1000.0f;
            metric[ch].slope_mv_s = (last_mv[ch] - first_mv[ch]) / total_time_s;
        } else {
            metric[ch].slope_mv_s = 0.0f;
        }
    }

    return 1U;
}

uint8_t AD630_MeasureWindow(AD630LockMetric metric[2], uint16_t sample_count, uint16_t interval_ms)
{
    return AD630_MeasureWindowServiced(metric, sample_count, interval_ms, 0, 0);
}

/*
 * This function performs ADS8688 GPIO software-SPI read frames.
 * It must be called from main-loop context only, never from an ISR.
 */
uint8_t get_phaseV(float *vot, uint8_t chnum, uint8_t times)
{
    uint8_t i = 0;
    uint32_t t[2] = {0U, 0U};
    uint16_t v[2] = {0U, 0U};

    if ((vot == 0) || (chnum == 0U) || (chnum > 2U) || (times == 0U)) {
        return 0U;
    }

    if (ADS8688_BeginAccess() == 0U) {
        return 0U;
    }

    for (i = 0U; i < times; i++) {
        HAL_Delay(1);
        Get_AUTO_RST_Mode_Data(v, chnum);
        t[0] += v[0];
        t[1] += v[1];
    }

    t[0] /= times;
    t[1] /= times;

    vot[0] = ADS8688_CodeToMilliVolt((uint16_t)t[0]);
    vot[1] = ADS8688_CodeToMilliVolt((uint16_t)t[1]);

    ADS8688_EndAccess();
    return 1U;
}

/*
 * times: number of slope checks.
 * sample_num: samples averaged for each point.
 * delay_time: delay between the two points.
 * ADS8688 software SPI must stay in main-loop context.
 */
uint32_t get_deltV(uint8_t times, uint8_t sample_num, uint8_t delay_time)
{
    uint16_t i = 0U;
    uint16_t j = 0U;
    uint16_t value = 0U;
    uint32_t error = 0U;
    uint32_t min_err0r = 10000U;
    uint32_t temp;

    if ((times == 0U) || (sample_num == 0U)) {
        return 0U;
    }

    if (ADS8688_BeginAccess() == 0U) {
        return 0U;
    }

    for (j = 0U; j < times; j++) {
        long int first_error = 0;
        long int second_error = 0;

        for (i = 0U; i < sample_num; i++) {
            Get_AUTO_RST_Mode_Data(&value, 1U);
            first_error += value;
        }
        first_error /= sample_num;

        HAL_Delay(delay_time);

        for (i = 0U; i < sample_num; i++) {
            Get_AUTO_RST_Mode_Data(&value, 1U);
            second_error += value;
        }
        second_error /= sample_num;

        temp = (uint32_t)labs(first_error - second_error);

        if (temp < min_err0r) {
            min_err0r = temp;
        }
        error += temp;
    }

    ADS8688_EndAccess();
    return error / times;
}

/* =========================================================================
 * AD630 Debug Module (NEW — coexists with original H-contest PLL)
 * ========================================================================= */

#define AD630_DEBUG_WINDOW_SAMPLES  16U
#define AD630_DEBUG_WINDOW_INTERVAL 10U   /* ms between samples */
#define AD630_DEBUG_STATUS_PERIOD   200U  /* ms between status prints */
#define AD630_DEBUG_CAL_WINDOWS     8U    /* windows for target calibration */

AD630_DebugMode  g_ad630_mode = AD630_MODE_OFF;
AD630_DebugStatus g_ad630_dbg;

static float  ad630_target_mv      = 0.0f;
static int    ad630_control_polarity = 1;
static uint32_t s_dbg_last_update  = 0;
static uint32_t s_dbg_last_status  = 0;

/* ---- Target setpoint API ---- */
void AD630_SetTargetMv(float target_mv)
{
    ad630_target_mv = target_mv;
    printf("[AD630] target_mv = %.3f\r\n", (double)target_mv);
}

float AD630_GetTargetMv(void)
{
    return ad630_target_mv;
}

/* ---- Calibrate target ---- */
void AD630_CalibrateTarget(void)
{
    AD630LockMetric metric[2];
    double sum = 0.0;
    uint8_t i, ok = 0;

    printf("[AD630] Calibrating target over %u windows...\r\n",
           (unsigned int)AD630_DEBUG_CAL_WINDOWS);

    for (i = 0U; i < AD630_DEBUG_CAL_WINDOWS; i++) {
        if (AD630_MeasureWindowServiced(metric,
                                        AD630_DEBUG_WINDOW_SAMPLES,
                                        AD630_DEBUG_WINDOW_INTERVAL,
                                        0, 0) != 0U) {
            sum += (double)metric[0].mean_mv;
            ok++;
        }
        HAL_Delay(5);
    }

    if (ok > 0U) {
        ad630_target_mv = (float)(sum / (double)ok);
        g_ad630_dbg.target_mv = ad630_target_mv;
        g_ad630_dbg.error_mv = 0.0f;
        printf("[AD630] Calibrated target_mv = %.3f mV (%u windows)\r\n",
               (double)ad630_target_mv, (unsigned int)ok);
    } else {
        printf("[AD630] Calibration FAILED — all windows returned error\r\n");
    }
}

/* ---- Structured status output ---- */
void AD630_PrintDebugStatus(void)
{
    const char *mode_str;

    /* Rate-limit to avoid UART flood */
    if (HAL_GetTick() - s_dbg_last_status < AD630_DEBUG_STATUS_PERIOD) {
        return;
    }
    s_dbg_last_status = HAL_GetTick();

    switch (g_ad630_mode) {
        case AD630_MODE_OFF:         mode_str = "OFF";         break;
        case AD630_MODE_MEASURE:     mode_str = "MEASURE";     break;
        case AD630_MODE_CLOSED_LOOP: mode_str = "CLOSED_LOOP"; break;
        default:                     mode_str = "?";           break;
    }

    printf("\r\n[AD630]\r\n");
    printf("mode=%s\r\n", mode_str);
    printf("freq_A_detected=%.2f\r\n", (double)Basic_Freq[0]);
    printf("dds_A_freq=%.2f\r\n",
           (double)Basic_Freq[0]
           + (double)freq_word_offset[0] * 100000.0 / 2684355.0);
    printf("mean_mv=%.3f\r\n", (double)g_ad630_dbg.mean_mv);
    printf("target_mv=%.3f\r\n", (double)g_ad630_dbg.target_mv);
    printf("error_mv=%.3f\r\n", (double)g_ad630_dbg.error_mv);
    printf("rms_mv=%.3f\r\n", (double)g_ad630_dbg.rms_mv);
    printf("slope_mv_s=%.3f\r\n", (double)g_ad630_dbg.slope_mv_s);
    printf("freq_word_offset=%ld\r\n", (long)g_ad630_dbg.freq_word_offset);
    printf("control_direction=%d\r\n", ad630_control_polarity);
    printf("valid=%u\r\n", (unsigned int)g_ad630_dbg.valid);
    printf("locked=%u\r\n", (unsigned int)g_ad630_dbg.locked);
}

/* ---- Periodic update (call from main loop) ---- */
void AD630_DebugUpdate(void)
{
    AD630LockMetric metric[2];

    /* Rate-limit */
    if (HAL_GetTick() - s_dbg_last_update < AD630_DEBUG_WINDOW_INTERVAL) {
        return;
    }
    s_dbg_last_update = HAL_GetTick();

    /* OFF mode: do nothing */
    if (g_ad630_mode == AD630_MODE_OFF) {
        return;
    }

    /* Check ADS8688 health */
    if (ads8688_fault != ADS8688_OK) {
        g_ad630_dbg.valid = 0U;
        return;
    }

    /* Run measurement window on CH0 */
    if (AD630_MeasureWindowServiced(metric,
                                    AD630_DEBUG_WINDOW_SAMPLES,
                                    AD630_DEBUG_WINDOW_INTERVAL,
                                    0, 0) != 0U)
    {
        g_ad630_dbg.mean_mv  = metric[0].mean_mv;
        g_ad630_dbg.rms_mv   = metric[0].ac_rms_mv;
        g_ad630_dbg.min_mv   = metric[0].min_mv;
        g_ad630_dbg.max_mv   = metric[0].max_mv;
        g_ad630_dbg.slope_mv_s = metric[0].slope_mv_s;
        g_ad630_dbg.valid    = 1U;

        /* Signed phase error */
        g_ad630_dbg.error_mv =
            g_ad630_dbg.mean_mv - g_ad630_dbg.target_mv;
    }
    else {
        g_ad630_dbg.valid = 0U;
    }
}

/* ---- Text command parser ---- */
int AD630_ParseCommand(const char *line)
{
    char cmd[16] = {0}, a1[16] = {0}, a2[16] = {0};
    int n = sscanf(line, "%15s %15s %15s", cmd, a1, a2);

    /* ---- ad630 ---- */
    if (strcmp(cmd, "ad630") == 0) {
        if (n >= 2 && strcmp(a1, "off") == 0) {
            g_ad630_mode = AD630_MODE_OFF;
            printf("[AD630] Mode = OFF (original H-contest)\r\n");
            return 1;
        }
        if (n >= 2 && strcmp(a1, "measure") == 0) {
            g_ad630_mode = AD630_MODE_MEASURE;
            printf("[AD630] Mode = MEASURE (measure-only, DDS frozen)\r\n");
            return 1;
        }
        if (n >= 2 && strcmp(a1, "lock") == 0) {
            g_ad630_mode = AD630_MODE_CLOSED_LOOP;
            printf("[AD630] Mode = CLOSED_LOOP\r\n");
            return 1;
        }
        if (n >= 2 && strcmp(a1, "status") == 0) {
            AD630_PrintDebugStatus();
            return 1;
        }
        if (n >= 2 && strcmp(a1, "zero") == 0) {
            AD630_CalibrateTarget();
            return 1;
        }
        if (n >= 2 && strcmp(a1, "polarity") == 0 && n >= 3) {
            if (strcmp(a2, "normal") == 0) {
                ad630_control_polarity = 1;
                printf("[AD630] Polarity = normal\r\n");
            } else if (strcmp(a2, "invert") == 0) {
                ad630_control_polarity = -1;
                printf("[AD630] Polarity = inverted\r\n");
            }
            return 1;
        }
    }

    /* ---- pll reset ---- */
    if (strcmp(cmd, "pll") == 0 && n >= 2 && strcmp(a1, "reset") == 0) {
        g_ad630_dbg.locked = 0U;
        g_ad630_dbg.freq_word_offset = 0;
        printf("[AD630] PLL reset\r\n");
        return 1;
    }

    return 0; /* not recognised */
}
