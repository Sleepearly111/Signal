#ifndef INST_FREQ_H
#define INST_FREQ_H

#include <stdint.h>

/* ================================================================
 * 原始捕获间隔统计 (raw TIM8 tick diffs)
 *
 * 用途: 2PSK 跳变 / 2FSK 跳变 / 比较器毛刺检测 /
 *        采集质量诊断。不做频率换算, 仅分类计数。
 * ================================================================ */
typedef struct {
    int  raw_count;             /* 有效间隔数量 (cap_count-1)            */
    int  zero_count;            /* 间隔=0                                */
    int  short_count;           /* 间隔 < CAP_MIN_CARRIER_TICKS          */
    int  long_count;            /* 间隔 > CAP_MAX_INTERVAL_TICKS         */
    int  normal_count;          /* 正常间隔数                            */
    uint16_t min_ticks;         /* 最小间隔 (排除零)                     */
    uint16_t max_ticks;         /* 最大间隔                              */
} CapIntervalStats_t;

/**
 * [稳态频率路径] — CW / FM / 2FSK 频率中心
 *
 * 40 载波周期平均瞬时频率序列 (FREQ_DECIM=5, ICPSC_DIV8=8)。
 * 任一 FREQ_DECIM 窗口内的任意单个间隔超过 FREQ_VALID_MAX_TICKS
 * 则该频率点被跳过, 不会被输出。
 *
 * 返回: 实际有效频率点数 (≤ max_out)。
 *
 * 限制: TIM8 为 16-bit 计数器, 无外部时间戳。
 * cap_buf 相邻 uint16_t 模减对单次溢出正确,
 * 但对 >65535 ticks (390us) 的无边沿间隙无法分辨。
 * 此类间隙会被 uint16 模减映射为伪正常间隔,
 * 调用者应结合 CapIntervalStats_t 的 long_count 排除。
 */
int InstFreq_Compute(const uint16_t *cap, int n_cap, float *out, int max_out);

/**
 * [原始异常事件路径] — 2PSK / 2FSK 跳变 / 比较器毛刺
 *
 * 扫描 cap_buf 所有相邻差值 (uint16_t 模减),
 * 按阈值分为: 零 / 过短 / 正常 / 过长, 不做频率换算。
 *
 * 注意: 2PSK 半周期相位异常 (diff 偏移 ±42 ticks ≈ 6.25%)
 * 落在 "正常" 区间内, 不会被 short/long 过滤;
 * 具体的 2PSK 检测应在此基础上做游程或跳变分析。
 */
void CapInterval_Analyze(const uint16_t *cap, int n_cap,
                         CapIntervalStats_t *stats);

#endif
