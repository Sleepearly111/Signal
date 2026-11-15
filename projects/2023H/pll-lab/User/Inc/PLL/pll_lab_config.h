/**
 * @file    pll_lab_config.h
 * @brief   PLL_Lab_2026 unified hardware configuration constants.
 *
 * This file is the SINGLE SOURCE OF TRUTH for:
 *   - AD9833 DDS master clock frequency
 *   - ADS8688 ADC input range
 *
 * All modules (AD9833.c, main.c, pid_phase.c, ADS8688.c) must
 * reference these macros.  Do NOT duplicate numeric constants.
 *
 * ============================================================================
 * AD9833 FREQUENCY WORD FORMULA
 * ============================================================================
 *
 *   freq_word = f_out_hz * (2^28 / MCLK)
 *
 *   where 2^28 = 268,435,456 is the AD9833 frequency register width.
 *
 *   MCLK is provided by STM32 MCO2 (PC9), configured in set_clock() as
 *   HSE / 2.  With the on-board 8 MHz crystal:
 *
 *       MCLK = 8,000,000 / 2 = 4,000,000 Hz
 *
 *   Resulting LSB resolution:  4e6 / 2^28 ≈ 0.0149 Hz
 *   With 16-slot dither (×64 equiv. levels): ≈ 0.00023 Hz
 *
 *   ** WARNING **  This value MUST be verified with an oscilloscope.
 *   If HSE ≠ 8 MHz or MCO2 divider is changed, update this macro
 *   and rebuild.  Incorrect MCLK will silently produce wrong DDS
 *   output frequencies.
 *
 * ============================================================================
 * ADS8688 CODE-TO-VOLTAGE FORMULA
 * ============================================================================
 *
 *   The ADS8688 returns 16-bit unsigned codes:
 *       code = 0       → negative full-scale  (-Vref * gain)
 *       code ≈ 32768   → 0 V
 *       code = 65535   → positive full-scale  (+Vref * gain)
 *
 *   voltage_mV = (code - 32768) / 32768 * RANGE_MV
 *
 *   Current hardware: internal 4.096 V reference, range register = ±10.24 V
 *   (ADS8688_RANGE_PM_10V24 = 0x00).  RANGE_MV = 10240.
 *
 *   ** WARNING **  Verify actual range register setting in ads8688_set().
 *   If changed to ±5.12 V (±2.5×Vref), RANGE_MV = 5120.
 *   If changed to ±2.56 V (±0.625×Vref), RANGE_MV = 2560.
 *
 *   pid_phase.c lock/drift thresholds are defined in TRUE MILLIVOLTS.
 *   Changing RANGE_MV without updating ADS8688_CodeToMilliVolt() will
 *   silently corrupt all voltage-dependent PLL decisions.
 *
 * ============================================================================
 */

#ifndef PLL_LAB_CONFIG_H
#define PLL_LAB_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * AD9833 DDS Master Clock
 * -------------------------------------------------------------------------- */
/** MCO2 output frequency in Hz, fed to both AD9833 chips.
 *  @note  Verify with oscilloscope on PC9 before trusting PLL data. */
#define PLL_LAB_AD9833_MCLK_HZ  4000000UL

/** Precomputed: 2^28 / MCLK  (used for Hz → frequency word conversion).
 *  Do NOT edit directly — derived from PLL_LAB_AD9833_MCLK_HZ. */
#define PLL_LAB_AD9833_HZ_TO_WORD  (268435456.0 / (double)PLL_LAB_AD9833_MCLK_HZ)

/* --------------------------------------------------------------------------
 * ADS8688 ADC Input Range
 * -------------------------------------------------------------------------- */
/** Full-scale voltage range in millivolts (positive/negative).
 *  Matches ads8688_set() range register.
 *  @note  10240 = ±10.24 V (0x00 register, internal 4.096 V ref × 2.5)  */
#define PLL_LAB_ADS8688_RANGE_MV  10240.0f

#if defined(__GNUC__) || defined(__clang__)
/* Compile-time sanity: if someone defines MCLK = 0 we catch it here. */
_Static_assert(PLL_LAB_AD9833_MCLK_HZ > 0U,
               "PLL_LAB_AD9833_MCLK_HZ must be > 0");
#endif

#ifdef __cplusplus
}
#endif

#endif /* PLL_LAB_CONFIG_H */
