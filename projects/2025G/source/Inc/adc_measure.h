#ifndef ADC_MEASURE_H
#define ADC_MEASURE_H

#include <stdint.h>

/* 测量结果 */
typedef struct {
    float rms_v;    /* 有效值 (V) */
    float vpp;      /* 峰峰值 (V) */
    float dc_v;     /* 直流偏置 (V) */
} ADC_MeasureResult;

/* 启动一次幅度测量 (非阻塞，在主循环中轮询) */
void ADC_Measure_Start(uint8_t ads8688_channel);

/* 测量是否完成 */
uint8_t ADC_Measure_IsDone(void);

/* 获取测量结果 (完成后调用) */
ADC_MeasureResult ADC_Measure_GetResult(void);

/* 测量模块初始化 */
void ADC_Measure_Init(void);

/* 在主循环中每次 ADS8688_Service() 后调用，收集采样点 */
void ADC_Measure_Service(void);

#endif /* ADC_MEASURE_H */
