/**
 * @file    filter_comp.h
 * @brief   二阶RC低通滤波器频率幅度补偿模块
 *
 * 滤波参数: R=10kΩ, C=220pF, fc≈72.34kHz (单级)
 * 二阶级联近似: H(f)=1/(1+(f/fc)^2)
 * 补偿系数:     K(f)=1+(f/fc)^2
 *
 * 补偿只作用于幅度, 不影响相位/时延/DDS频率。
 * 用户参数保持原始值, 补偿为内部自动行为。
 */
#ifndef FILTER_COMP_H
#define FILTER_COMP_H

#include <stdint.h>

/* 查询频率补偿系数 K(f), 支持30k~40kHz线性插值 */
float Filter_GetCompensation(uint32_t freq_hz);

/* 查询滤波器衰减 H(f) = 1/K(f) */
float Filter_GetAttenuation(uint32_t freq_hz);

/* 检查补偿后幅度是否超出DAC安全范围, 0=OK, -1=越界 */
int Filter_CheckClipping(uint32_t freq_hz, uint16_t vrms_mv);

#endif /* FILTER_COMP_H */
