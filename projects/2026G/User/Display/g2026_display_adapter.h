#ifndef G2026_DISPLAY_ADAPTER_H
#define G2026_DISPLAY_ADAPTER_H

#include <stdint.h>

#include "g2026_measurement_dsp.h"
#include "stm32f4xx_hal.h"

void G2026Display_Init(UART_HandleTypeDef *uart);
void G2026Display_Process(void);

void G2026Display_SubmitMeasurement(
    uint32_t acquisition_id,
    const float *calibrated_samples_mv,
    uint16_t sample_count,
    float corrected_sample_rate_hz,
    const G2026DspResult *result,
    float apparatus_input_scale);

void G2026Display_OnUartRxComplete(UART_HandleTypeDef *uart);
void G2026Display_OnUartTxComplete(UART_HandleTypeDef *uart);
void G2026Display_OnUartError(UART_HandleTypeDef *uart);

#endif
