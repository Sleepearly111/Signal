#include "inst_freq.h"
#include "app_config.h"
#include <stdbool.h>

/* ================================================================
 * InstFreq_Compute — 稳态频率路径
 *
 * 公式:
 *   ticks         = sum(cap[i+k+1] - cap[i+k]), k=0..FREQ_DECIM-1
 *   frequency_hz  = TIM8_CLK_HZ * (CAP_IC_PRESCALER * FREQ_DECIM)
 *                   / ticks
 *                 = 168e6 * 40 / ticks
 *
 * 每点覆盖 40 个载波周期 (FREQ_DECIM=5 × CAP_IC_PRESCALER=8)。
 *
 * === 有效性过滤 (Phase 2A.1) ===
 * 在 FREQ_DECIM 窗口内逐个检查相邻 uint16_t 差值:
 *   差值 == 0                              → 无效 (计数器异常)
 *   差值 > FREQ_VALID_MAX_TICKS (5000)     → 无效 (丢边沿/2ASK低电平)
 *   差值 < CAP_MIN_CARRIER_TICKS (200)     → 无效 (TIM8 回绕/噪声/2PSK)
 *
 * 注意: 2PSK 半周期相位跳变的 diff 变化量 (~±42 ticks)
 * 从 672→630(-42) 或 672→714(+42), 均落在 [200, 5000] 内,
 * 不会被过滤。2PSK 毛刺检测应使用 CapInterval_Analyze() 的
 * 原始间隔输出, 而非本函数的平均频率输出。
 *
 * === 已知 uint16_t 限制 ===
 * >390us 无边沿 → uint16 模减产生虚假正常值。
 * CapInterval_Analyze 的 long_count + cap_count 比值可辅助推断。
 * ================================================================ */

int InstFreq_Compute(const uint16_t *cap, int n_cap, float *out, int max_out)
{
    int n = 0;
    for (int i = 0; i + FREQ_DECIM < n_cap && n < max_out; i += FREQ_DECIM) {
        uint32_t ticks = 0;
        bool valid = true;

        for (int k = 0; k < FREQ_DECIM; k++) {
            uint16_t d = (uint16_t)(cap[i + k + 1] - cap[i + k]);

            /* 零间隔: 计数器异常 */
            if (d == 0) {
                valid = false;
                break;
            }

            /* 过短间隔: 比较器毛刺 / 定时器异常 */
            if (d < CAP_MIN_CARRIER_TICKS) {
                valid = false;
                break;
            }

            /* 超长间隔: 丢边沿 / 2ASK 低电平 / TIM8 回绕 */
            if (d > FREQ_VALID_MAX_TICKS) {
                valid = false;
                break;
            }

            ticks += d;
        }

        if (!valid || ticks == 0) continue;

        out[n++] = (float)TIM8_CLK_HZ
                 * (float)(CAP_IC_PRESCALER * FREQ_DECIM)
                 / (float)ticks;
    }
    return n;
}

/* ================================================================
 * CapInterval_Analyze — 原始异常事件路径
 *
 * 不做频率换算, 仅输出间隔分类统计。
 * 2PSK 相位跳变 (diff 偏移 ±42 ticks) 落在 normal 区间内,
 * 后续检测应在此原始数据上做游程分析。
 *
 * === uint16_t 回绕记录 ===
 * 若 cap_count 远小于预期值 (如 2ASK 场景),
 * long_count 中值被低估 — 回绕使实际长间隔映射为
 * [0, 65535] 范围内的值, 大部分被错误分类为 normal/short。
 * 该限制在 CapIntervalStats_t.raw_count << cap_stats.expected_count
 * 时对调用者可见。
 * ================================================================ */

void CapInterval_Analyze(const uint16_t *cap, int n_cap,
                         CapIntervalStats_t *stats)
{
    stats->raw_count    = 0;
    stats->zero_count   = 0;
    stats->short_count  = 0;
    stats->long_count   = 0;
    stats->normal_count = 0;
    stats->min_ticks    = 0xFFFFU;
    stats->max_ticks    = 0;

    if (n_cap < 2) return;

    for (int i = 0; i < n_cap - 1; i++) {
        uint16_t d = (uint16_t)(cap[i + 1] - cap[i]);

        if (d == 0) {
            stats->zero_count++;
            continue;
        }

        stats->raw_count++;

        if (d < CAP_MIN_CARRIER_TICKS)
            stats->short_count++;
        else if (d > CAP_MAX_INTERVAL_TICKS)
            stats->long_count++;
        else
            stats->normal_count++;

        if (d < stats->min_ticks) stats->min_ticks = d;
        if (d > stats->max_ticks) stats->max_ticks = d;
    }

    if (stats->min_ticks == 0xFFFFU)
        stats->min_ticks = 0;
}
