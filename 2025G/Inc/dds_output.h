#ifndef DDS_OUTPUT_H
#define DDS_OUTPUT_H

#include <stdint.h>

/* 初始化: AD9833 + 内部DAC(VGA控制) */
void DDS_Output_Init(void);

/* 设置 AD9833 输出频率 (Hz) */
void DDS_SetFrequency(uint32_t freq_hz);

/* VGA 增益控制: dB 方式 */
void VGA_SetGain_dB(float gain_dB);

/* VGA 增益控制: 线性倍数方式 (自动转dB) */
void VGA_SetGain_Linear(float gain_linear);

/* 便捷组合: 同时设频率 + 线性增益 */
void DDS_Output_Config(uint32_t freq_hz, float gain_linear);

#endif /* DDS_OUTPUT_H */
