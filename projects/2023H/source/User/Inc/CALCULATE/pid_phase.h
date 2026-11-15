#ifndef __PID_PHASE_H
#define __PID_PHASE_H
#include "main.h"

/* =========================================================================
 * AD630 Lock Metric (original — kept for existing PLL code)
 * ========================================================================= */
typedef struct {
    float mean_mv;
    float ac_rms_mv;
    float min_mv;
    float max_mv;
    float pp_mv;
    float slope_mv_s;
} AD630LockMetric;

typedef void (*AD630WindowService)(void *context);

uint8_t get_phaseV(float *vot, uint8_t chnum, uint8_t times);
uint32_t get_deltV(uint8_t times, uint8_t sample_num, uint8_t delay_time);
uint8_t AD630_MeasureWindow(AD630LockMetric metric[2], uint16_t sample_count, uint16_t interval_ms);
uint8_t AD630_MeasureWindowServiced(AD630LockMetric metric[2],
                                    uint16_t sample_count,
                                    uint16_t interval_ms,
                                    AD630WindowService service,
                                    void *context);
void PLLPhase_ResetRuntime(uint16_t delay_flag[2], uint32_t real_freq_word[4]);
uint8_t PLLPhase_UpdateWindow(uint8_t ch,
                              const AD630LockMetric *metric,
                              uint16_t delay_flag[2],
                              uint32_t real_freq_word[4]);
uint8_t PLLPhase_IsLocked(uint8_t ch);
uint8_t FLL_IsHoldValid(uint8_t ch);
void PLLPhase_Reset(const float lock_mv[2], const int initial_delay_flag[2], const uint32_t initial_freq_word[4]);
void PLLPhase_Update(uint8_t ch, uint16_t ads_code, int delay_flag[2], uint32_t real_freq_word[4]);

/* =========================================================================
 * AD630 Debug Mode (NEW — coexists with original H-contest flow)
 * ========================================================================= */
typedef enum {
    AD630_MODE_OFF        = 0,  /* Original 2023H logic, no AD630 auto-adjust */
    AD630_MODE_MEASURE    = 1,  /* FFT + Demultiplex → DDS init, AD630 measure only */
    AD630_MODE_CLOSED_LOOP= 2   /* FFT seed → PLL correction → DDS frequency trim */
} AD630_DebugMode;

/* =========================================================================
 * AD630 Debug Status (measurable snapshot, refreshed each window)
 * ========================================================================= */
typedef struct {
    /* ---- Detector raw metrics (from ADS8688 window) ---- */
    float   mean_mv;          /* signed DC average */
    float   rms_mv;           /* AC RMS (noise indicator) */
    float   min_mv;
    float   max_mv;
    float   slope_mv_s;       /* drift indicator */

    /* ---- Error signal ---- */
    float   target_mv;        /* calibrated setpoint */
    float   error_mv;         /* mean_mv - target_mv (SIGNED) */

    /* ---- Control ---- */
    int32_t freq_word_offset; /* current PLL frequency-word trim */
    uint8_t valid;            /* last measurement window succeeded */
    uint8_t locked;           /* lock criteria met */
} AD630_DebugStatus;

/* =========================================================================
 * AD630 Debug Globals
 * ========================================================================= */
extern AD630_DebugMode  g_ad630_mode;
extern AD630_DebugStatus g_ad630_dbg;

/* =========================================================================
 * AD630 Debug API
 * ========================================================================= */

/** Set target voltage (mV). Negative to set, but typically near 0. */
void AD630_SetTargetMv(float target_mv);

/** Get current target voltage (mV). */
float AD630_GetTargetMv(void);

/** Calibrate target by averaging AD630 output over N windows.
 *  Call when A' reference phase is at the desired lock point. */
void AD630_CalibrateTarget(void);

/** Print structured [AD630] status block to USART2. */
void AD630_PrintDebugStatus(void);

/** Parse a text command from UART. Returns 1 if recognised, 0 if not. */
int AD630_ParseCommand(const char *line);

/** Call periodically from main loop (rate-limited internally). */
void AD630_DebugUpdate(void);


#endif
