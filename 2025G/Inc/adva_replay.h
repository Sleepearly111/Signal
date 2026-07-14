#ifndef ADVA_REPLAY_H
#define ADVA_REPLAY_H

#include "app_config.h"
#include "arm_math.h"
#include <stdint.h>

/* ===== 发挥(2): 根据学到的 FIR 模型复现未知电路输出 =====
 * ADC 采信号发生器输入(高阻≥100kΩ) → FFT 测频+判波型
 * → 查 FIR 表 → FIR 滤波 → FFT 谐波提取 → 傅里叶合成整周期
 * → DAC DMA 循环输出(PA5/DAC_CH2), TIM6 触发控制频率。
 * 连续运行,输入频率或波型变化时自动更新输出。 */

/* 波形类型(由基波幅值判定, 2Vpp 信号源) */
#define WAVE_SINE      1
#define WAVE_TRIANGLE  2
#define WAVE_SQUARE    3

/* ===== API ===== */
void AdvReplay_Init(void);      /* 系统启动时调用 */
void AdvReplay_Start(void);     /* 一键启动复现 */
void AdvReplay_Service(void);   /* 主循环调用,持续推进 */
void AdvReplay_Stop(void);      /* 停止输出 */
uint8_t AdvReplay_IsRunning(void);

#endif /* ADVA_REPLAY_H */
