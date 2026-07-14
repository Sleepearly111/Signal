#ifndef __PID_PHASE_H
#define __PID_PHASE_H
#include "main.h"

typedef struct {
    float mean_mv;
    float ac_rms_mv;
    float min_mv;
    float max_mv;
    float pp_mv;
    float slope_mv_s;
} AD630LockMetric;

uint8_t get_phaseV(float *vot, uint8_t chnum, uint8_t times);
uint32_t get_deltV(uint8_t times, uint8_t sample_num, uint8_t delay_time);
uint8_t AD630_MeasureWindow(AD630LockMetric metric[2], uint16_t sample_count, uint16_t interval_ms);
void PLLPhase_ResetRuntime(uint16_t delay_flag[2], uint32_t real_freq_word[4]);
uint8_t PLLPhase_UpdateWindow(uint8_t ch,
                              const AD630LockMetric *metric,
                              uint16_t delay_flag[2],
                              uint32_t real_freq_word[4]);
uint8_t PLLPhase_IsLocked(uint8_t ch);
void PLLPhase_Reset(const float lock_mv[2], const int initial_delay_flag[2], const uint32_t initial_freq_word[4]);
void PLLPhase_Update(uint8_t ch, uint16_t ads_code, int delay_flag[2], uint32_t real_freq_word[4]);


#endif

