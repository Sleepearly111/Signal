/**
 * @file    signal_lock.c
 * @brief   Correlation-lock implementation for C = A + B signal separation.
 *
 * Uses AD630 as a correlation detector:
 *   C  ──> AD630 SIG IN
 *   DDS_REF (AD9833-A) ──> AD630 REF IN
 *   AD630 VOUT ──> LPF ──> ADS8688 CH0 ──> PI controller ──> DDS_REF + DDS_OUT
 *
 * DDS_OUT (AD9833-B) produces the reconstructed A' signal, synchronised
 * to DDS_REF with an optional output phase offset and waveform selection.
 */

#include "signal_lock.h"
#include "signal_lock_config.h"
#include "pid_phase.h"
#include "ADS8688.h"
#include "AD9833.h"
#include "pll_lab_config.h"
#include "usart.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

/* =========================================================================
 * Internal state
 * ========================================================================= */

static SignalLockCandidate  s_candidate;
static SignalLockStatus     s_status;

static double   s_integrator_hz;      /* PI integral accumulator (Hz) */
static double   s_last_error_mv;      /* for debug / derivative if needed */
static uint32_t s_last_update_tick;   /* HAL_GetTick() at last PI update */
static uint32_t s_stable_count;       /* consecutive windows meeting lock criteria */
static uint32_t s_fault_count;        /* consecutive windows with ADC fault */
static int      s_ref_phase_override; /* -1 = no override; 0..359 = manual deg */

static uint8_t  s_initialised = 0;

/* Pre-computed constants */
static double   s_hz_to_word;         /* 2^28 / MCLK */
static uint32_t s_base_freq_word_ref; /* seed frequency word for DDS_REF */
static uint32_t s_base_freq_word_out; /* seed frequency word for DDS_OUT */

/* --------------------------------------------------------------------------
 * Forward declarations
 * -------------------------------------------------------------------------- */
static void apply_dds(void);
static void update_controller(double error_mv, double dt_s);

/* =========================================================================
 * Init / Config
 * ========================================================================= */

void SignalLock_Init(void)
{
    memset(&s_candidate, 0, sizeof(s_candidate));
    memset(&s_status,    0, sizeof(s_status));

    s_candidate.seed_freq_hz            = 10000.0;   /* safe default */
    s_candidate.output_amplitude        = 1.0;
    s_candidate.output_phase_offset_deg = 0.0;
    s_candidate.detector_target_mv      = SIGNAL_LOCK_DEFAULT_TARGET_MV;
    s_candidate.waveform_type           = SIGNAL_WAVE_SINE;

    s_status.mode             = SIGNAL_LOCK_MEASURE_ONLY;
    s_status.state            = SIGNAL_LOCK_IDLE;
    s_status.seed_freq_hz     = s_candidate.seed_freq_hz;
    s_status.control_polarity = SIGNAL_LOCK_CONTROL_POLARITY;
    s_status.detector_target_mv = s_candidate.detector_target_mv;

    s_integrator_hz      = 0.0;
    s_last_error_mv      = 0.0;
    s_last_update_tick   = 0;
    s_stable_count       = 0;
    s_fault_count        = 0;
    s_ref_phase_override = -1;

    s_hz_to_word = PLL_LAB_AD9833_HZ_TO_WORD;

    /* Pre-compute base frequency words */
    s_base_freq_word_ref = (uint32_t)(s_hz_to_word * s_candidate.seed_freq_hz);
    s_base_freq_word_out = s_base_freq_word_ref;

    s_initialised = 1;
}

void SignalLock_SetCandidate(const SignalLockCandidate *candidate)
{
    if (candidate == NULL) return;

    memcpy(&s_candidate, candidate, sizeof(SignalLockCandidate));

    s_status.seed_freq_hz       = s_candidate.seed_freq_hz;
    s_status.detector_target_mv = s_candidate.detector_target_mv;

    s_base_freq_word_ref = (uint32_t)(s_hz_to_word * s_candidate.seed_freq_hz);
    s_base_freq_word_out = s_base_freq_word_ref;

    /* Reset integrator and controller on new candidate */
    s_integrator_hz    = 0.0;
    s_last_error_mv    = 0.0;
    s_stable_count     = 0;
    s_status.state     = SIGNAL_LOCK_IDLE;
    s_status.locked    = 0;
}

void SignalLock_SetMode(SignalLockMode mode)
{
    s_status.mode = mode;
    if (mode != SIGNAL_LOCK_CLOSED_LOOP) {
        s_status.state  = SIGNAL_LOCK_IDLE;
        s_status.locked = 0;
    }
}

void SignalLock_Start(void)
{
    if (s_status.mode != SIGNAL_LOCK_CLOSED_LOOP) return;

    s_integrator_hz  = 0.0;
    s_last_error_mv  = 0.0;
    s_stable_count   = 0;
    s_fault_count    = 0;
    s_status.state   = SIGNAL_LOCK_ACQUIRE;
    s_status.locked  = 0;
    s_status.freq_correction_hz   = 0.0;
    s_status.freq_word_correction = 0;
    s_status.phase_correction_deg = 0.0;
}

void SignalLock_Stop(void)
{
    s_status.state  = SIGNAL_LOCK_IDLE;
    s_status.locked = 0;
}

void SignalLock_Reset(void)
{
    SignalLock_Init();
    SignalLock_SetCandidate(&s_candidate);
}

void SignalLock_CalibrateZero(void)
{
    uint8_t i;
    double sum_mv = 0.0;
    AD630LockMetric metric[2];

    /* Take 8 measurement windows and average CH0 mean_mv */
    for (i = 0; i < 8U; i++) {
        if (AD630_MeasureWindowServiced(metric,
                                        SIGNAL_LOCK_WINDOW_SAMPLE_COUNT,
                                        SIGNAL_LOCK_WINDOW_INTERVAL_MS,
                                        0, 0) != 0U)
        {
            sum_mv += (double)metric[0].mean_mv;
        }
        HAL_Delay(5);
    }

    s_candidate.detector_target_mv     = sum_mv / 8.0;
    s_status.detector_target_mv        = s_candidate.detector_target_mv;
    s_status.phase_error_mv            = 0.0;

    printf("[CAL] detector_target_mv = %.3f mV (averaged over 8 windows)\r\n",
           s_status.detector_target_mv);
}

const SignalLockStatus *SignalLock_GetStatus(void)
{
    return &s_status;
}

void SignalLock_SetRefPhaseOverride(double deg)
{
    if (deg < 0.0 || deg >= 360.0) {
        s_ref_phase_override = -1;
        return;
    }
    s_ref_phase_override = (int)deg;
    adjust_channel_phase(0, deg, 0);   /* DDS_REF only */
}

void SignalLock_SetPolarity(int polarity)
{
    if (polarity >= 0) {
        s_status.control_polarity = 1;
    } else {
        s_status.control_polarity = -1;
    }
    printf("[CMD] Polarity = %d\r\n", s_status.control_polarity);
}

/* =========================================================================
 * DDS Update  (unified: both channels from one set of corrections)
 * ========================================================================= */

/**
 * @brief Apply corrected frequency and phase to both DDS channels.
 *
 * DDS_REF (A, ch0): freq = seed + correction,  phase = correction_phase
 * DDS_OUT (B, ch1): freq = seed + correction,  phase = correction_phase + offset
 *
 * Uses synchronous together_freq() / together_phase() where possible,
 * then sets waveform individually if they differ.
 */
static void apply_dds(void)
{
    double corrected_hz;
    uint32_t freq_word;
    double ref_phase, out_phase;
    extern uint8_t force;
    extern uint8_t wave_flag;

    corrected_hz = s_candidate.seed_freq_hz + s_status.freq_correction_hz;
    if (corrected_hz < 0.0) corrected_hz = 0.0;

    freq_word = (uint32_t)(s_hz_to_word * corrected_hz);

    /* Phase: correction applied to both; DDS_OUT gets extra offset */
    ref_phase = s_status.phase_correction_deg;
    out_phase = s_status.phase_correction_deg + s_candidate.output_phase_offset_deg;

    /* Clamp phases to [0, 360) */
    while (ref_phase < 0.0)   ref_phase  += 360.0;
    while (ref_phase >= 360.0) ref_phase -= 360.0;
    while (out_phase < 0.0)   out_phase  += 360.0;
    while (out_phase >= 360.0) out_phase -= 360.0;

    /* Override ref phase if manual scan active */
    if (s_ref_phase_override >= 0) {
        ref_phase = (double)s_ref_phase_override;
    }

    /* ---- Synchronous frequency write ---- */
    force = 1U;   /* direct frequency-word mode */
    together_freq(freq_word, freq_word);

    /* ---- Synchronous phase write ---- */
    together_phase(ref_phase, out_phase);

    /* ---- Waveform: DDS_REF always sine (best for AD630 comparator); DDS_OUT per candidate ---- */
    if (s_candidate.waveform_type == SIGNAL_WAVE_TRIANGLE) {
        wave_flag = 1;
        AD9833_SetWave(AD9833_OUT_TRIANGLE);
        wave_flag = 0;
        AD9833_SetWave(AD9833_OUT_SINUS);
    } else if (s_candidate.waveform_type == SIGNAL_WAVE_SQUARE) {
        wave_flag = 1;
        AD9833_SetWave(AD9833_OUT_MSB);
        wave_flag = 0;
        AD9833_SetWave(AD9833_OUT_SINUS);
    } else {
        /* Both sine — already set by default after init */
    }

    s_status.corrected_freq_hz = corrected_hz;
    s_status.freq_word_correction = (int32_t)(freq_word - s_base_freq_word_ref);
}

/* =========================================================================
 * PI Controller
 * ========================================================================= */

static void update_controller(double error_mv, double dt_s)
{
    double ki_term;
    double correction;

    if (dt_s <= 0.0) dt_s = 0.010;   /* fallback 10 ms */

    /* Apply polarity */
    error_mv *= (double)s_status.control_polarity;

    /* Proportional term */
    correction = SIGNAL_LOCK_KP_HZ_PER_MV * error_mv;

    /* Integral term with clamping */
    ki_term = SIGNAL_LOCK_KI_HZ_PER_MV_S * error_mv * dt_s;
    s_integrator_hz += ki_term;

    if (s_integrator_hz >  SIGNAL_LOCK_INTEGRAL_LIMIT_HZ)
        s_integrator_hz =  SIGNAL_LOCK_INTEGRAL_LIMIT_HZ;
    if (s_integrator_hz < -SIGNAL_LOCK_INTEGRAL_LIMIT_HZ)
        s_integrator_hz = -SIGNAL_LOCK_INTEGRAL_LIMIT_HZ;

    correction += s_integrator_hz;

    /* Clamp total correction */
    if (correction >  SIGNAL_LOCK_FREQ_CORRECTION_LIMIT_HZ)
        correction =  SIGNAL_LOCK_FREQ_CORRECTION_LIMIT_HZ;
    if (correction < -SIGNAL_LOCK_FREQ_CORRECTION_LIMIT_HZ)
        correction = -SIGNAL_LOCK_FREQ_CORRECTION_LIMIT_HZ;

    s_status.freq_correction_hz = correction;
    s_last_error_mv = error_mv;
}

/* =========================================================================
 * Lock State Machine
 * ========================================================================= */

/**
 * @brief Evaluate lock criteria from current metrics.
 * @return 1 if conditions met this window, 0 otherwise.
 */
static uint8_t evaluate_lock_criteria(void)
{
    if (fabs(s_status.phase_error_mv) < SIGNAL_LOCK_ERROR_LOCK_TH_MV &&
        s_status.detector_rms_mv      < SIGNAL_LOCK_NOISE_LOCK_TH_MV) {
        return 1;
    }
    return 0;
}

/* =========================================================================
 * Main Update
 * ========================================================================= */

void SignalLock_Update(void)
{
    AD630LockMetric metric[2];
    uint32_t now;
    double dt_s;
    uint8_t lock_ok;

    if (!s_initialised) return;

    /* --- Rate-limit: run at UPDATE_PERIOD_MS --- */
    now = HAL_GetTick();
    if (now - s_last_update_tick < SIGNAL_LOCK_UPDATE_PERIOD_MS) {
        return;
    }
    dt_s = (double)(now - s_last_update_tick) / 1000.0;
    s_last_update_tick = now;

    /* --- Fault detection --- */
    if (ads8688_fault != ADS8688_OK) {
        s_fault_count++;
        if (s_fault_count >= SIGNAL_LOCK_FAULT_WINDOW_COUNT) {
            s_status.state = SIGNAL_LOCK_FAULT;
            s_status.locked = 0;
        }
        return;
    }
    s_fault_count = 0;

    /* --- Sample AD630 via ADS8688 --- */
    if (AD630_MeasureWindowServiced(metric,
                                    SIGNAL_LOCK_WINDOW_SAMPLE_COUNT,
                                    SIGNAL_LOCK_WINDOW_INTERVAL_MS,
                                    0, 0) == 0U)
    {
        /* Window failed — keep last state, don't count as fault */
        return;
    }

    /* --- Update metrics --- */
    s_status.detector_mean_mv  = (double)metric[0].mean_mv;
    s_status.detector_rms_mv   = (double)metric[0].ac_rms_mv;
    s_status.detector_slope_mv_s = (double)metric[0].slope_mv_s;

    s_status.phase_error_mv =
        s_status.detector_mean_mv - s_status.detector_target_mv;

    /* --- Mode dispatch --- */
    if (s_status.mode == SIGNAL_LOCK_DISABLED) {
        return;
    }

    if (s_status.mode == SIGNAL_LOCK_MEASURE_ONLY) {
        /* Observe only — no PI, no DDS changes */
        s_status.state = SIGNAL_LOCK_IDLE;
        return;
    }

    /* === CLOSED_LOOP === */
    if (s_status.state == SIGNAL_LOCK_IDLE || s_status.state == SIGNAL_LOCK_FAULT) {
        /* Not started — keep DDS at last setting */
        return;
    }

    /* --- Run PI controller --- */
    update_controller(s_status.phase_error_mv, dt_s);

    /* --- Apply to DDS --- */
    apply_dds();

    /* --- Lock state machine --- */
    lock_ok = evaluate_lock_criteria();

    switch (s_status.state) {

    case SIGNAL_LOCK_ACQUIRE:
        if (lock_ok) {
            s_stable_count++;
            if (s_stable_count >= SIGNAL_LOCK_STABLE_WINDOW_COUNT) {
                s_status.state  = SIGNAL_LOCK_LOCKED;
                s_status.locked = 1;
            } else {
                s_status.state = SIGNAL_LOCK_TRACK;
            }
        } else {
            s_stable_count = 0;
        }
        break;

    case SIGNAL_LOCK_TRACK:
        if (lock_ok) {
            s_stable_count++;
            if (s_stable_count >= SIGNAL_LOCK_STABLE_WINDOW_COUNT) {
                s_status.state  = SIGNAL_LOCK_LOCKED;
                s_status.locked = 1;
            }
        } else {
            s_stable_count = 0;
        }
        break;

    case SIGNAL_LOCK_LOCKED:
        if (!lock_ok) {
            s_stable_count = 0;
            s_status.state  = SIGNAL_LOCK_TRACK;   /* drop back to tracking */
            s_status.locked = 0;
        }
        break;

    default:
        break;
    }
}
