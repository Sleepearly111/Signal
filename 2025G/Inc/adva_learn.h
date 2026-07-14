#ifndef ADVA_LEARN_H
#define ADVA_LEARN_H

#include "app_config.h"
#include "arm_math.h"
#include <stdint.h>

/* ===== 发挥(1): 未知电路自主学习建模 =====
 * 按键启动 → 扫频(1k~60k@200Hz + 60k~1M@10kHz)
 * → LMS 逐频点收敛出 32阶 FIR → 存表
 * → 100Hz/1MHz 两点幅值比判滤波类型 → 串口屏显示
 * 总时间 <2 分钟(题目要求), 全程不借助外部仪器。 */

/* 滤波类型 */
#define FILTER_LOW_PASS    1
#define FILTER_HIGH_PASS   2
#define FILTER_BAND_PASS   3
#define FILTER_BAND_STOP   4

/* 扫频参数(与参考方案一致) */
#define SWEEP_FREQ_START    1000.0f   /* 1kHz */
#define SWEEP_FREQ_1_END    60100.0f  /* 第一段终点 Hz(参考代码上界) */
#define SWEEP_FREQ_2_START  60000.0f  /* 第二段起点 */
#define SWEEP_FREQ_2_END    1010000.0f/* 第二段终点(过了1M即止) */
#define SWEEP_STEP_1        200       /* 1k~60k 步进 200Hz */
#define SWEEP_STEP_2        10000     /* 60k~1M 步进 10kHz */

/* FIR 系数表大小: (60100-1000)/200+1 = 296 组 */
#define FIR_TABLE_ROWS  (((uint32_t)SWEEP_FREQ_1_END - (uint32_t)SWEEP_FREQ_START) / SWEEP_STEP_1 + 1)

/* ===== API ===== */
void AdvLearn_Init(void);       /* 系统启动时调用一次 */
void AdvLearn_Start(void);      /* 按下"学习键"后调用, 启动扫频学习 */
void AdvLearn_Service(void);    /* 主循环中调用, 非阻塞推进状态机 */
uint8_t AdvLearn_IsDone(void);  /* 学习完成返回 1 */
uint8_t AdvLearn_GetFilterType(void); /* 返回 FILTER_xxx 或 0(尚未判出) */

/* FIR 系数表(外部可读, 发挥2 查表用) */
extern float32_t g_fir_table[FIR_TABLE_ROWS][LMS_NUM_TAPS];

#endif /* ADVA_LEARN_H */
