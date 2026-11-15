/*
 * tjc_display.h
 *
 * TJC display and UART adapter. It does not perform calibration, FFT, RMS,
 * peak-to-peak measurement, frequency estimation, or harmonic detection.
 * The upstream measurement module submits completed results and calibrated
 * voltage samples; this module only adapts them to the HMI protocol.
 */
#ifndef PRESENTATION_INC_TJC_DISPLAY_H_
#define PRESENTATION_INC_TJC_DISPLAY_H_

#include "usart.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TJC_DISPLAY_TIME_SOURCE_SAMPLE_COUNT (4096U)
#define TJC_DISPLAY_MAX_SPECTRUM_LINES        (3U)

typedef enum
{
    TJC_TIME_PERIOD_ONE = 1U,
    TJC_TIME_PERIOD_THREE = 3U
} TjcTimePeriodMode;

typedef enum
{
    TJC_TIME_STATUS_OK = 0,
    TJC_TIME_STATUS_BAD_ARGUMENT,
    TJC_TIME_STATUS_WAVEFORM_INVALID,
    TJC_TIME_STATUS_SPECTRUM_NOT_CONFIGURED,
    TJC_TIME_STATUS_SPECTRUM_INVALID
} TjcDisplayStatus;

/** @brief One final, upstream-recognized spectral line; no detection occurs here. */
typedef struct
{
    uint16_t harmonic_order;
    float frequency_hz;
    float amplitude_vpk;
} TjcDisplaySpectrumLine;

/** @brief HMI text is fully prepared by the upstream result formatter. */
typedef struct
{
    char rows[6][64];
} TjcDisplayTextRows;

/**
 * @brief Completed-analysis input for page0.
 * @note voltage_samples_v contains one real calibrated 4096-point voltage
 * window in volts. The caller owns the array only until
 * TjcDisplay_SubmitTime() returns.
 */
typedef struct
{
    uint32_t acquisition_id;
    uint8_t valid;
    const float *voltage_samples_v;
    uint16_t sample_count;
    float sample_rate_hz;
    float fundamental_hz;
    TjcDisplayTextRows text;
} TjcDisplayTimeSubmission;

/**
 * @brief Completed-analysis input for page1. Lines are already identified by
 * the upstream FFT/analysis layer; this module only places them on the
 * configured display columns.
 */
typedef struct
{
    uint32_t acquisition_id;
    uint8_t valid;
    uint8_t line_count;
    TjcDisplaySpectrumLine lines[TJC_DISPLAY_MAX_SPECTRUM_LINES];
    float full_scale_vpk;
    TjcDisplayTextRows text;
} TjcDisplaySpectrumSubmission;

/** @brief Maps one physical voltage to the TJC Waveform 0..255 coordinate. */
uint8_t TjcDisplay_MapTimeVoltage(float signal_v, float display_full_scale_v);

void TjcDisplay_Init(UART_HandleTypeDef *uart);
void TjcDisplay_Service(void);

/** @brief Converts a completed 4096-point calibrated voltage window to configured display bytes. */
TjcDisplayStatus TjcDisplay_SubmitTime(const TjcDisplayTimeSubmission *submission);

/** @brief Converts completed, upstream-identified spectrum lines to configured display bytes. */
TjcDisplayStatus TjcDisplay_SubmitSpectrum(const TjcDisplaySpectrumSubmission *submission);

/** @brief Requests a page0 cle/addt transaction using the cached latest submission. */
void TjcDisplay_RequestTimeWaveformRefresh(void);

/** @brief Requests a page1 cle/addt transaction using the cached latest submission. */
void TjcDisplay_RequestSpectrumWaveformRefresh(void);

uint8_t TjcDisplay_GetCurrentPageId(void);
bool TjcDisplay_TakeTimePeriodRequest(TjcTimePeriodMode *period_mode);

void TjcDisplay_OnUartRxComplete(UART_HandleTypeDef *uart);
void TjcDisplay_OnUartTxComplete(UART_HandleTypeDef *uart);
void TjcDisplay_OnUartError(UART_HandleTypeDef *uart);

#ifdef __cplusplus
}
#endif
#endif /* PRESENTATION_INC_TJC_DISPLAY_H_ */
