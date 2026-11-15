/**
 * @file    signal_lock.h
 * @brief   Correlation-based lock for A-component in composite signal C = A + B.
 *
 * Hardware chain:
 *   C  ──> AD630 SIG IN       (external composite signal)
 *   DDS_REF ──> AD630 REF IN  (local carrier, AD9833-A)
 *   AD630 VOUT ──> LPF ──> ADS8688 CH0 ──> STM32 PID ──> DDS_REF freq/phase
 *   DDS_OUT ──> A' output    (reconstructed A, AD9833-B)
 *
 * The AD630 DC output after LPF represents correlation between the A
 * component in C and the local reference.  It is NOT the A waveform itself.
 *
 * This module:
 *   - Wraps AD630_MeasureWindowServiced() for low-level sampling
 *   - Implements a PI controller on phase_error_mv = mean_mv - target_mv
 *   - Drives both DDS channels from a single set of state variables
 */

#ifndef SIGNAL_LOCK_H
#define SIGNAL_LOCK_H

#include "main.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * Public types
 * -------------------------------------------------------------------------- */

/** Operating mode */
typedef enum {
    SIGNAL_LOCK_DISABLED     = 0,
    SIGNAL_LOCK_MEASURE_ONLY = 1,
    SIGNAL_LOCK_CLOSED_LOOP  = 2
} SignalLockMode;

/** Waveform selection for DDS_OUT (DDS_REF always uses comparator-friendly) */
typedef enum {
    SIGNAL_WAVE_SINE     = 0,
    SIGNAL_WAVE_TRIANGLE = 1,
    SIGNAL_WAVE_SQUARE   = 2
} SignalWaveType;

/** Lock state machine */
typedef enum {
    SIGNAL_LOCK_IDLE    = 0,
    SIGNAL_LOCK_ACQUIRE = 1,
    SIGNAL_LOCK_TRACK   = 2,
    SIGNAL_LOCK_LOCKED  = 3,
    SIGNAL_LOCK_FAULT   = 4
} SignalLockState;

/** Candidate signal parameters (set externally via FFT recognition or UART) */
typedef struct {
    double          seed_freq_hz;           /**< Nominal A frequency (from FFT / user) */
    double          output_amplitude;       /**< Reserved for future amplitude config */
    double          output_phase_offset_deg;/**< Extra phase offset on DDS_OUT only */
    double          detector_target_mv;     /**< Target DC at quadrature null */
    SignalWaveType  waveform_type;          /**< Waveform for DDS_OUT (A') */
} SignalLockCandidate;

/** Live status snapshot (read by main.c for UART output) */
typedef struct {
    SignalLockMode  mode;
    SignalLockState state;

    double          seed_freq_hz;
    double          corrected_freq_hz;

    double          detector_mean_mv;      /**< ADS8688 CH0 window average */
    double          detector_target_mv;    /**< Calibrated zero-cross target */
    double          phase_error_mv;        /**< mean_mv - target_mv (signed) */

    double          detector_rms_mv;       /**< AC RMS in window */
    double          detector_slope_mv_s;   /**< Drift slope */

    double          freq_correction_hz;    /**< PI output */
    int32_t         freq_word_correction;  /**< In DDS LSB units */
    double          phase_correction_deg;  /**< Phase trim applied to both DDS */

    int             control_polarity;      /**< +1 or -1 */

    uint8_t         locked;                /**< 1 if LOCKED state */
} SignalLockStatus;

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/** One-time init.  Must be called after AD9833_Init() and ads8688_set(). */
void SignalLock_Init(void);

/** Set the candidate signal parameters.  Overwrites previous. */
void SignalLock_SetCandidate(const SignalLockCandidate *candidate);

/** Change operating mode. */
void SignalLock_SetMode(SignalLockMode mode);

/** Enter ACQUIRE → TRACK sequence (only effective in CLOSED_LOOP mode). */
void SignalLock_Start(void);

/** Return to IDLE.  DDS outputs freeze at last values. */
void SignalLock_Stop(void);

/** Full reset: re-init state machine, clear integrator, reload candidate. */
void SignalLock_Reset(void);

/**
 * @brief Main update tick.  Call from main loop at ≥ SIGNAL_LOCK_UPDATE_PERIOD_MS.
 *
 * In MEASURE_ONLY mode: takes a sample window and updates status.
 * In CLOSED_LOOP mode: runs PI controller and adjusts DDS.
 */
void SignalLock_Update(void);

/**
 * @brief Calibrate detector_zero by averaging N windows at current DDS_REF phase.
 *
 * The user should first set seed_freq_hz = A frequency, mode = MEASURE_ONLY,
 * then manually scan DDS_REF phase (via dds_ref_phase command) to find the
 * 90° quadrature null, then issue "cal detector_zero".
 *
 * The measured mean_mv is saved as detector_target_mv.
 */
void SignalLock_CalibrateZero(void);

/** Return a read-only pointer to the live status. */
const SignalLockStatus *SignalLock_GetStatus(void);

/**
 * @brief Direct DDS_REF phase override (degrees).
 *
 * Used ONLY for manual AD630 response scanning (0°, 90°, 180°, 270°).
 * Not a normal closed-loop interface.
 *
 * @param deg  Phase in degrees [0, 360).  Negative to release override.
 */
void SignalLock_SetRefPhaseOverride(double deg);

/**
 * @brief Set control polarity at runtime.
 * @param polarity  +1 or -1
 */
void SignalLock_SetPolarity(int polarity);

#ifdef __cplusplus
}
#endif

#endif /* SIGNAL_LOCK_H */
