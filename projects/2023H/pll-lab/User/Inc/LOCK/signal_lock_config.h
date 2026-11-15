/**
 * @file    signal_lock_config.h
 * @brief   Correlation-lock configuration constants for PLL_Lab_2026.
 *
 * All tunable parameters for the signal_lock module live here.
 * Do NOT scatter magic numbers in signal_lock.c or main.c.
 *
 * ============================================================================
 * SIGNAL CHAIN (2023H correlation detection)
 * ============================================================================
 *
 *   C = A + B  ──> AD630 SIG IN
 *   DDS_REF     ──> AD630 REF IN
 *   AD630 VOUT  ──> LPF ──> ADS8688 CH0 ──> STM32
 *
 *   AD630 output after LPF is a signed DC level proportional to the
 *   correlation between the A component in C and the local DDS_REF.
 *
 *   At the ~90° quadrature operating point the DC output crosses zero
 *   and has maximum slope vs. phase error — this is the target.
 *
 * ============================================================================
 * CONTROL LOOP (PI)
 * ============================================================================
 *
 *   phase_error_mv = detector_mean_mv - detector_target_mv
 *   freq_correction += Kp * error + Ki * integral(error) * dt
 *
 *   frequency correction is clamped to ±FREQ_CORRECTION_LIMIT_HZ
 *   integral term is clamped to ±INTEGRAL_LIMIT_HZ
 */

#ifndef SIGNAL_LOCK_CONFIG_H
#define SIGNAL_LOCK_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * Default detector target  (mV)
 *
 * AD630 + LPF + ADS8688 will have some DC offset even at the quadrature
 * null.  Calibrate with "cal detector_zero" and the runtime value overrides
 * this default.
 * -------------------------------------------------------------------------- */
#define SIGNAL_LOCK_DEFAULT_TARGET_MV    0.0f

/* --------------------------------------------------------------------------
 * PI controller gains
 * -------------------------------------------------------------------------- */
/** Proportional gain:  Hz correction per mV of phase error */
#define SIGNAL_LOCK_KP_HZ_PER_MV        0.02f

/** Integral gain:  Hz/s correction per mV of phase error */
#define SIGNAL_LOCK_KI_HZ_PER_MV_S      0.5f

/** Maximum absolute integral wind-up (Hz) */
#define SIGNAL_LOCK_INTEGRAL_LIMIT_HZ   50.0f

/** Maximum absolute frequency correction (Hz) */
#define SIGNAL_LOCK_FREQ_CORRECTION_LIMIT_HZ  200.0f

/** Maximum absolute phase correction (degrees) */
#define SIGNAL_LOCK_PHASE_CORRECTION_LIMIT_DEG  180.0f

/* --------------------------------------------------------------------------
 * Lock / noise thresholds
 * -------------------------------------------------------------------------- */
/** |phase_error_mv| must be below this to consider locked (mV) */
#define SIGNAL_LOCK_ERROR_LOCK_TH_MV     20.0f

/** detector_rms_mv must be below this to consider locked (mV) */
#define SIGNAL_LOCK_NOISE_LOCK_TH_MV     50.0f

/** Number of consecutive stable windows required to declare LOCKED */
#define SIGNAL_LOCK_STABLE_WINDOW_COUNT   5U

/* --------------------------------------------------------------------------
 * Timing
 * -------------------------------------------------------------------------- */
/** Control update period (ms).  The PI runs every N ms.
 *  5–20 ms is recommended for signals up to 100 kHz.
 *  Must be an integer divisor of the sample window duration. */
#define SIGNAL_LOCK_UPDATE_PERIOD_MS     10U

/** Status print period (ms).  Keep at 100–200 ms to avoid UART flood. */
#define SIGNAL_LOCK_STATUS_PERIOD_MS     200U

/* --------------------------------------------------------------------------
 * Sample window  (passed through to pid_phase AD630_MeasureWindowServiced)
 * -------------------------------------------------------------------------- */
/** Number of ADS8688 sample pairs per measurement window */
#define SIGNAL_LOCK_WINDOW_SAMPLE_COUNT  16U

/** Interval between sample pairs (ms).
 *  Total window = SAMPLE_COUNT * INTERVAL_MS.
 *  At 16 × 10 ms = 160 ms with 10 ms control period: 16 updates per window. */
#define SIGNAL_LOCK_WINDOW_INTERVAL_MS   10U

/* --------------------------------------------------------------------------
 * Detector polarity
 * -------------------------------------------------------------------------- */
/**
 * SIGNAL_LOCK_CONTROL_POLARITY:
 *   +1 : increasing detector voltage → increase frequency
 *   -1 : increasing detector voltage → decrease frequency
 *
 * Determined experimentally: scan DDS_REF phase at fixed frequency
 * and observe AD630 DC slope near the 90° quadrature point.
 */
#define SIGNAL_LOCK_CONTROL_POLARITY     1

/* --------------------------------------------------------------------------
 * Fault detection
 * -------------------------------------------------------------------------- */
/** If ADS8688 is in fault for this many consecutive windows, enter FAULT */
#define SIGNAL_LOCK_FAULT_WINDOW_COUNT    3U

#ifdef __cplusplus
}
#endif

#endif /* SIGNAL_LOCK_CONFIG_H */
