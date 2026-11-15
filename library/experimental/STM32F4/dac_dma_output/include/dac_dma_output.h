#ifndef DAC_DMA_OUTPUT_H
#define DAC_DMA_OUTPUT_H

#include <stdint.h>

#include "stm32f4xx_hal_dma.h"
#include "stm32f4xx_hal_dac.h"
#include "stm32f4xx_hal_tim.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DAC_DMA_OUTPUT_STATE_UNINITIALIZED = 0,
    DAC_DMA_OUTPUT_STATE_STOPPED,
    DAC_DMA_OUTPUT_STATE_RUNNING,
    DAC_DMA_OUTPUT_STATE_ERROR
} DacDmaOutput_State;

typedef enum {
    DAC_DMA_OUTPUT_OK = 0,
    DAC_DMA_OUTPUT_ERROR_NULL,
    DAC_DMA_OUTPUT_ERROR_CONFIG,
    DAC_DMA_OUTPUT_ERROR_STATE,
    DAC_DMA_OUTPUT_ERROR_HAL
} DacDmaOutput_Result;

typedef struct {
    TIM_HandleTypeDef *timer;
    uint32_t trigger_source;
} DacDmaOutput_TriggerConfig;

typedef struct {
    DAC_HandleTypeDef *dac;
    DMA_HandleTypeDef *channel_a_dma;
    DMA_HandleTypeDef *channel_b_dma;
    DacDmaOutput_TriggerConfig trigger;
    uint32_t channel_a;
    uint32_t channel_b;
    uint32_t alignment;
    uint32_t *channel_a_buffer;
    uint32_t *channel_b_buffer;
    uint32_t buffer_length;
} DacDmaOutput_Config;

typedef struct {
    DacDmaOutput_Config config;
    DacDmaOutput_State state;
    uint8_t initialized;
} DacDmaOutput;

/* Validate and store an already configured STM32F4 HAL peripheral setup. */
DacDmaOutput_Result DacDmaOutput_Init(
    DacDmaOutput *output,
    const DacDmaOutput_Config *config);

/* Start both DAC DMA streams, then enable their common trigger timer. */
DacDmaOutput_Result DacDmaOutput_Start(DacDmaOutput *output);

/* Stop the common trigger first, then stop both DAC DMA streams. */
DacDmaOutput_Result DacDmaOutput_Stop(DacDmaOutput *output);

/*
 * Replace both wave tables. If running, the service performs a controlled
 * stop and restart. Both channels always use the same sample count.
 */
DacDmaOutput_Result DacDmaOutput_UpdateBuffer(
    DacDmaOutput *output,
    uint32_t *channel_a_buffer,
    uint32_t *channel_b_buffer,
    uint32_t buffer_length);

DacDmaOutput_State DacDmaOutput_GetStatus(const DacDmaOutput *output);

#ifdef __cplusplus
}
#endif

#endif /* DAC_DMA_OUTPUT_H */
