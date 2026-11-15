#ifndef G2026_MOCK_PROFILE_H
#define G2026_MOCK_PROFILE_H

/*
 * STM32-only DSP test profile.
 *
 * Amplitudes are peak values, matching the 2026G statement.
 * Frequencies must be integer multiples of 500 Hz and must not exceed 500 kHz.
 * This profile generates signed, centered ADC codes after the hypothetical
 * FPGA filtering/decimation stage. It does not model the analog front end.
 */
#define G2026_MOCK_ACTUAL_SAMPLE_RATE_HZ 1999600U
#define G2026_MOCK_REPORTED_SAMPLE_RATE_HZ 2000000U
#define G2026_MOCK_MILLIVOLTS_PER_CODE 0.01F
#define G2026_MOCK_COMPONENT_COUNT 3U
#define G2026_MOCK_GAIN_ERROR_PPM (-6000.0F)
#define G2026_MOCK_DC_OFFSET_MV 0.17F
#define G2026_MOCK_NOISE_PEAK_MV 0.12F
#define G2026_MOCK_NOISE_SEED 0x20260729U

#define G2026_MOCK_C1_FREQUENCY_HZ 37500.0F
#define G2026_MOCK_C1_PEAK_MV 36.0F
#define G2026_MOCK_C1_PHASE_DEG 0.0F

#define G2026_MOCK_C2_FREQUENCY_HZ 112500.0F
#define G2026_MOCK_C2_PEAK_MV 18.0F
#define G2026_MOCK_C2_PHASE_DEG 0.0F

#define G2026_MOCK_C3_FREQUENCY_HZ 300000.0F
#define G2026_MOCK_C3_PEAK_MV 7.0F
#define G2026_MOCK_C3_PHASE_DEG 0.0F

#endif
