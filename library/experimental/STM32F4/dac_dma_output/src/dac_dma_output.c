#include "dac_dma_output.h"

static DMA_HandleTypeDef *DacDmaOutput_GetLinkedDma(
    DAC_HandleTypeDef *dac,
    uint32_t channel)
{
    if (channel == DAC_CHANNEL_1) {
        return dac->DMA_Handle1;
    }

    if (channel == DAC_CHANNEL_2) {
        return dac->DMA_Handle2;
    }

    return NULL;
}

static int DacDmaOutput_IsAlignmentValid(uint32_t alignment)
{
    return (alignment == DAC_ALIGN_12B_R) ||
           (alignment == DAC_ALIGN_12B_L) ||
           (alignment == DAC_ALIGN_8B_R);
}

static DacDmaOutput_Result DacDmaOutput_ValidateConfig(
    const DacDmaOutput_Config *config)
{
    if ((config == NULL) ||
        (config->dac == NULL) ||
        (config->channel_a_dma == NULL) ||
        (config->channel_b_dma == NULL) ||
        (config->trigger.timer == NULL) ||
        (config->channel_a_buffer == NULL) ||
        (config->channel_b_buffer == NULL)) {
        return DAC_DMA_OUTPUT_ERROR_NULL;
    }

    if ((config->channel_a == config->channel_b) ||
        (config->buffer_length == 0U) ||
        !DacDmaOutput_IsAlignmentValid(config->alignment)) {
        return DAC_DMA_OUTPUT_ERROR_CONFIG;
    }

    if ((DacDmaOutput_GetLinkedDma(config->dac, config->channel_a) !=
         config->channel_a_dma) ||
        (DacDmaOutput_GetLinkedDma(config->dac, config->channel_b) !=
         config->channel_b_dma)) {
        return DAC_DMA_OUTPUT_ERROR_CONFIG;
    }

    return DAC_DMA_OUTPUT_OK;
}

static DacDmaOutput_Result DacDmaOutput_CheckInitialized(
    const DacDmaOutput *output)
{
    if (output == NULL) {
        return DAC_DMA_OUTPUT_ERROR_NULL;
    }

    if (output->initialized == 0U) {
        return DAC_DMA_OUTPUT_ERROR_STATE;
    }

    return DAC_DMA_OUTPUT_OK;
}

static DacDmaOutput_Result DacDmaOutput_StartHardware(
    DacDmaOutput *output)
{
    HAL_StatusTypeDef hal_status;

    hal_status = HAL_DAC_Start_DMA(
        output->config.dac,
        output->config.channel_a,
        output->config.channel_a_buffer,
        output->config.buffer_length,
        output->config.alignment);
    if (hal_status != HAL_OK) {
        output->state = DAC_DMA_OUTPUT_STATE_ERROR;
        return DAC_DMA_OUTPUT_ERROR_HAL;
    }

    hal_status = HAL_DAC_Start_DMA(
        output->config.dac,
        output->config.channel_b,
        output->config.channel_b_buffer,
        output->config.buffer_length,
        output->config.alignment);
    if (hal_status != HAL_OK) {
        (void)HAL_DAC_Stop_DMA(
            output->config.dac, output->config.channel_a);
        output->state = DAC_DMA_OUTPUT_STATE_ERROR;
        return DAC_DMA_OUTPUT_ERROR_HAL;
    }

    hal_status = HAL_TIM_Base_Start(output->config.trigger.timer);
    if (hal_status != HAL_OK) {
        (void)HAL_DAC_Stop_DMA(
            output->config.dac, output->config.channel_a);
        (void)HAL_DAC_Stop_DMA(
            output->config.dac, output->config.channel_b);
        output->state = DAC_DMA_OUTPUT_STATE_ERROR;
        return DAC_DMA_OUTPUT_ERROR_HAL;
    }

    output->state = DAC_DMA_OUTPUT_STATE_RUNNING;
    return DAC_DMA_OUTPUT_OK;
}

static DacDmaOutput_Result DacDmaOutput_StopHardware(
    DacDmaOutput *output)
{
    HAL_StatusTypeDef timer_status;
    HAL_StatusTypeDef channel_a_status;
    HAL_StatusTypeDef channel_b_status;

    timer_status = HAL_TIM_Base_Stop(output->config.trigger.timer);
    channel_a_status = HAL_DAC_Stop_DMA(
        output->config.dac, output->config.channel_a);
    channel_b_status = HAL_DAC_Stop_DMA(
        output->config.dac, output->config.channel_b);

    if ((timer_status != HAL_OK) ||
        (channel_a_status != HAL_OK) ||
        (channel_b_status != HAL_OK)) {
        output->state = DAC_DMA_OUTPUT_STATE_ERROR;
        return DAC_DMA_OUTPUT_ERROR_HAL;
    }

    output->state = DAC_DMA_OUTPUT_STATE_STOPPED;
    return DAC_DMA_OUTPUT_OK;
}

DacDmaOutput_Result DacDmaOutput_Init(
    DacDmaOutput *output,
    const DacDmaOutput_Config *config)
{
    DacDmaOutput_Result result;

    if (output == NULL) {
        return DAC_DMA_OUTPUT_ERROR_NULL;
    }

    result = DacDmaOutput_ValidateConfig(config);
    if (result != DAC_DMA_OUTPUT_OK) {
        return result;
    }

    output->config = *config;
    output->state = DAC_DMA_OUTPUT_STATE_STOPPED;
    output->initialized = 1U;

    return DAC_DMA_OUTPUT_OK;
}

DacDmaOutput_Result DacDmaOutput_Start(DacDmaOutput *output)
{
    DacDmaOutput_Result result = DacDmaOutput_CheckInitialized(output);

    if (result != DAC_DMA_OUTPUT_OK) {
        return result;
    }

    if (output->state != DAC_DMA_OUTPUT_STATE_STOPPED) {
        return DAC_DMA_OUTPUT_ERROR_STATE;
    }

    return DacDmaOutput_StartHardware(output);
}

DacDmaOutput_Result DacDmaOutput_Stop(DacDmaOutput *output)
{
    DacDmaOutput_Result result = DacDmaOutput_CheckInitialized(output);

    if (result != DAC_DMA_OUTPUT_OK) {
        return result;
    }

    if (output->state == DAC_DMA_OUTPUT_STATE_STOPPED) {
        return DAC_DMA_OUTPUT_OK;
    }

    if (output->state != DAC_DMA_OUTPUT_STATE_RUNNING) {
        return DAC_DMA_OUTPUT_ERROR_STATE;
    }

    return DacDmaOutput_StopHardware(output);
}

DacDmaOutput_Result DacDmaOutput_UpdateBuffer(
    DacDmaOutput *output,
    uint32_t *channel_a_buffer,
    uint32_t *channel_b_buffer,
    uint32_t buffer_length)
{
    DacDmaOutput_Result result = DacDmaOutput_CheckInitialized(output);
    int was_running;

    if (result != DAC_DMA_OUTPUT_OK) {
        return result;
    }

    if ((channel_a_buffer == NULL) || (channel_b_buffer == NULL)) {
        return DAC_DMA_OUTPUT_ERROR_NULL;
    }

    if (buffer_length == 0U) {
        return DAC_DMA_OUTPUT_ERROR_CONFIG;
    }

    if ((output->state != DAC_DMA_OUTPUT_STATE_STOPPED) &&
        (output->state != DAC_DMA_OUTPUT_STATE_RUNNING)) {
        return DAC_DMA_OUTPUT_ERROR_STATE;
    }

    was_running = output->state == DAC_DMA_OUTPUT_STATE_RUNNING;
    if (was_running != 0) {
        result = DacDmaOutput_StopHardware(output);
        if (result != DAC_DMA_OUTPUT_OK) {
            return result;
        }
    }

    output->config.channel_a_buffer = channel_a_buffer;
    output->config.channel_b_buffer = channel_b_buffer;
    output->config.buffer_length = buffer_length;

    if (was_running != 0) {
        return DacDmaOutput_StartHardware(output);
    }

    return DAC_DMA_OUTPUT_OK;
}

DacDmaOutput_State DacDmaOutput_GetStatus(const DacDmaOutput *output)
{
    if ((output == NULL) || (output->initialized == 0U)) {
        return DAC_DMA_OUTPUT_STATE_UNINITIALIZED;
    }

    return output->state;
}
