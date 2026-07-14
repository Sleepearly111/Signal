#include "adc_measure.h"
#include "app_config.h"
#include "arm_math.h"
#include "math.h"

/* 复用 ADS8688 驱动 */
#include "ADS8688.h"

/* ===== 内部状态 ===== */
static float    sample_buf[MEASURE_SAMPLES];   /* 原始采样值 (mV) */
static float    sample_ac[MEASURE_SAMPLES];    /* 去直流后 (mV) */
static uint32_t sample_count;                   /* 已采点数 */
static uint8_t  measure_done;
static uint8_t  measure_active;
static uint8_t  target_channel;

/* 测量结果缓存 */
static ADC_MeasureResult last_result;

void ADC_Measure_Init(void)
{
    sample_count  = 0;
    measure_done  = 0;
    measure_active = 0;
    target_channel = 0;
}

void ADC_Measure_Start(uint8_t ads8688_channel)
{
    sample_count   = 0;
    measure_done   = 0;
    measure_active = 1;
    target_channel = ads8688_channel;  /* 0=CH0, 1=CH1 */

    /* 清除 ADS8688 数据就绪标志，避免读到旧数据 */
    ads8688_data_ready = 0;
}

uint8_t ADC_Measure_IsDone(void)
{
    return measure_done;
}

/*
 * 在主循环中每次 ADS8688_Service() 后调用
 * 当 ads8688_data_ready 时收集一个采样点
 */
void ADC_Measure_Service(void)
{
    if (!measure_active) return;
    if (!ads8688_data_ready) return;

    ads8688_data_ready = 0;

    /* 读取指定通道 ADC 码 → 转换为 mV */
    float mv = ADS8688_CodeToMilliVolt(v_suoxiang[target_channel]);
    sample_buf[sample_count] = mv;
    sample_count++;

    if (sample_count >= MEASURE_SAMPLES) {
        measure_active = 0;
        measure_done   = 1;

        /* 计算直流偏置 */
        float dc;
        arm_mean_f32(sample_buf, MEASURE_SAMPLES, &dc);

        /* 去直流 */
        for (uint32_t i = 0; i < MEASURE_SAMPLES; i++) {
            sample_ac[i] = sample_buf[i] - dc;
        }

        /* RMS (mV) */
        float rms_mv;
        arm_rms_f32(sample_ac, MEASURE_SAMPLES, &rms_mv);

        /* 转换为 V */
        last_result.rms_v = rms_mv / 1000.0f;
        last_result.vpp   = last_result.rms_v * 2.828427f;  /* RMS × 2√2 */
        last_result.dc_v  = dc / 1000.0f;
    }
}

ADC_MeasureResult ADC_Measure_GetResult(void)
{
    return last_result;
}
