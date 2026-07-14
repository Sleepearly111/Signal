#include "pid_phase.h"
#include "math.h"
#include "usart.h"
#include "ADS8688.h"
#include "stdlib.h"
#include "AD9833.h"
#include "calculate.h"

#define find_range 9
#define find_times 3
#define PLL_PHASE_CH_NUM 2U
#define PLL_PHASE_FREQ_WORD_LIMIT 5
#define AD630_LOCK_RMS_TH_MV      10.0f
#define AD630_LOCK_SLOPE_TH_MV_S  100.0f
#define AD630_LOCK_STABLE_NEED    3U

double judge1 = 0;
double judge2 = 0;

extern uint32_t Basic_Freq[2];
extern uint16_t Wave_Type[2];

static const float pll_kp = -0.1f;
static const float pll_ki = 0.05f;
static const float pll_kd = 0.0f;
static const float pll_max_corr_delay = 16.0f;
static const float pll_err_dead_mv[PLL_PHASE_CH_NUM] = {5.0f, 5.0f};
static const float pll_sign[PLL_PHASE_CH_NUM] = {1.0f, 1.0f};
static const int pll_search_dir[PLL_PHASE_CH_NUM] = {1, 1};

static uint8_t lock_confirmed[PLL_PHASE_CH_NUM] = {0U, 0U};
static uint8_t stable_count[PLL_PHASE_CH_NUM] = {0U, 0U};
static float v_target_mv[PLL_PHASE_CH_NUM] = {0.0f, 0.0f};
static float i_term_delay[PLL_PHASE_CH_NUM] = {0.0f, 0.0f};
static float prev_err_mv[PLL_PHASE_CH_NUM] = {0.0f, 0.0f};
static uint16_t base_delay_flag[PLL_PHASE_CH_NUM] = {0U, 0U};
static int freq_word_offset[PLL_PHASE_CH_NUM] = {0, 0};
static uint32_t base_freq_word[4] = {0U, 0U, 0U, 0U};

static int32_t PLLPhase_RoundToInt(float value)
{
    if (value >= 0.0f) {
        return (int32_t)(value + 0.5f);
    }

    return (int32_t)(value - 0.5f);
}

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

static void PLLPhase_ApplyDelayStep(uint8_t ch,
                                    int step,
                                    uint16_t delay_flag[2],
                                    uint32_t real_freq_word[4])
{
    int next_delay_flag;

    if ((ch >= PLL_PHASE_CH_NUM) || (delay_flag == 0) || (real_freq_word == 0) || (step == 0)) {
        return;
    }

    next_delay_flag = (int)delay_flag[ch] + step;
    if (next_delay_flag < 0) {
        if (freq_word_offset[ch] < PLL_PHASE_FREQ_WORD_LIMIT) {
            freq_word_offset[ch]++;
            delay_flag[ch] = 16U;
        } else {
            freq_word_offset[ch] = PLL_PHASE_FREQ_WORD_LIMIT;
            delay_flag[ch] = 0U;
        }
    } else if (next_delay_flag > 16) {
        if (freq_word_offset[ch] > -PLL_PHASE_FREQ_WORD_LIMIT) {
            freq_word_offset[ch]--;
            delay_flag[ch] = 0U;
        } else {
            freq_word_offset[ch] = -PLL_PHASE_FREQ_WORD_LIMIT;
            delay_flag[ch] = 16U;
        }
    } else {
        delay_flag[ch] = (uint16_t)next_delay_flag;
    }
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
        lock_confirmed[ch] = 0U;
        stable_count[ch] = 0U;
        v_target_mv[ch] = 0.0f;
        i_term_delay[ch] = 0.0f;
        prev_err_mv[ch] = 0.0f;
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
    float prev_i_term_delay;
    int32_t delay_corr;
    int step = 0;
    uint8_t stable_now;
    uint8_t locked_now;

    if ((ch >= PLL_PHASE_CH_NUM) || (metric == 0) || (delay_flag == 0) || (real_freq_word == 0)) {
        return 0U;
    }

    if (lock_confirmed[ch] == 0U) {
        stable_now = ((metric->ac_rms_mv < AD630_LOCK_RMS_TH_MV) &&
                      (fabsf(metric->slope_mv_s) < AD630_LOCK_SLOPE_TH_MV_S)) ? 1U : 0U;
        if (stable_now != 0U) {
            if (stable_count[ch] < AD630_LOCK_STABLE_NEED) {
                stable_count[ch]++;
            }
            if (stable_count[ch] >= AD630_LOCK_STABLE_NEED) {
                lock_confirmed[ch] = 1U;
                v_target_mv[ch] = metric->mean_mv;
                i_term_delay[ch] = 0.0f;
                prev_err_mv[ch] = 0.0f;
            }
        } else {
            stable_count[ch] = 0U;
            step = pll_search_dir[ch];
            PLLPhase_ApplyDelayStep(ch, step, delay_flag, real_freq_word);
        }

        PLLPhase_ApplyFreqWordOffset(ch, real_freq_word);
        printf("AD630_PLL ch=%d state=%s mean_mv=%.3f ac_rms_mv=%.3f pp_mv=%.3f slope_mv_s=%.3f target_mv=%.3f delay=%u offset=%d dir=%d\r\n",
               ch,
               (lock_confirmed[ch] != 0U) ? "LOCKED" : "SEARCH",
               metric->mean_mv,
               metric->ac_rms_mv,
               metric->pp_mv,
               metric->slope_mv_s,
               v_target_mv[ch],
               (unsigned int)delay_flag[ch],
               freq_word_offset[ch],
               step);
        return lock_confirmed[ch];
    }

    err_mv = (metric->mean_mv - v_target_mv[ch]) * pll_sign[ch];
    if (fabsf(err_mv) < pll_err_dead_mv[ch]) {
        err_mv = 0.0f;
    }

    prev_i_term_delay = i_term_delay[ch];
    i_term_delay[ch] += pll_ki * err_mv;
    if (i_term_delay[ch] > pll_max_corr_delay) {
        i_term_delay[ch] = pll_max_corr_delay;
    }
    if (i_term_delay[ch] < -pll_max_corr_delay) {
        i_term_delay[ch] = -pll_max_corr_delay;
    }

    delay_corr = PLLPhase_RoundToInt(i_term_delay[ch] + (pll_kp * err_mv) + (pll_kd * (err_mv - prev_err_mv[ch])));
    prev_err_mv[ch] = err_mv;

    if (delay_corr > 0) {
        step = -1;
    } else if (delay_corr < 0) {
        step = 1;
    } else {
        step = 0;
    }

    locked_now = lock_confirmed[ch];
    PLLPhase_ApplyDelayStep(ch, step, delay_flag, real_freq_word);
    if (((freq_word_offset[ch] == PLL_PHASE_FREQ_WORD_LIMIT) && (step < 0) && (delay_flag[ch] == 0U)) ||
        ((freq_word_offset[ch] == -PLL_PHASE_FREQ_WORD_LIMIT) && (step > 0) && (delay_flag[ch] == 16U))) {
        i_term_delay[ch] = prev_i_term_delay;
    }

    PLLPhase_ApplyFreqWordOffset(ch, real_freq_word);
    printf("AD630_PLL ch=%d state=%s mean_mv=%.3f ac_rms_mv=%.3f pp_mv=%.3f slope_mv_s=%.3f target_mv=%.3f delay=%u offset=%d dir=%d\r\n",
           ch,
           (locked_now != 0U) ? "LOCKED" : "SEARCH",
           metric->mean_mv,
           metric->ac_rms_mv,
           metric->pp_mv,
           metric->slope_mv_s,
           v_target_mv[ch],
           (unsigned int)delay_flag[ch],
           freq_word_offset[ch],
           step);

    return lock_confirmed[ch];
}

uint8_t PLLPhase_IsLocked(uint8_t ch)
{
    if (ch >= PLL_PHASE_CH_NUM) {
        return 0U;
    }

    return lock_confirmed[ch];
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

uint8_t AD630_MeasureWindow(AD630LockMetric metric[2], uint16_t sample_count, uint16_t interval_ms)
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

