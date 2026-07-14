#ifndef DSP_UTILS_H
#define DSP_UTILS_H

#include "arm_math.h"
#include <stdint.h>

/* ===== 发挥部分信号处理工具 =====
 * 用 CMSIS-DSP 标准 API 拼装: FFT/加窗/去直流/峰搜索/相位。
 * 参考方案的自研函数(find_fft_freq_amp/flat_top_window 等)
 * 在此重写为等价实现。 */

/* 一次性初始化(系统启动时调用) */
void dsp_init(void);

/* 去直流: data[in/out], 返回直流值 */
float dsp_remove_dc(float32_t *data, uint32_t len);

/* Flat-Top 平顶窗(5项余弦), 就地乘入 data[0..len-1] */
void dsp_flat_top_window(float32_t *data, uint32_t len);

/* RFFT → 归一化幅度谱
 * data[in]  : 实数时域(会被 FFT 过程修改)
 * mag[out]  : 幅度谱, 长度=fft_size/2+1, 已归一化(正弦峰值≈实际V)
 * 内部保存复数数据, 供 dsp_find_phase 使用 */
void dsp_fft_magnitude(float32_t *data, float32_t *mag, uint32_t fft_size);

/* 返回内部保存的复数 FFT 数据(interleaved), 用于相位提取 */
const float32_t* dsp_get_fft_complex(void);

/* 在幅度谱中找基波峰(跳过DC), 抛物线插值提高频率精度。
 * 返回频率(Hz), 同时可选填幅值(V)、bin号 */
float dsp_find_peak_freq(const float32_t *mag, uint32_t fft_size,
                         float sample_rate, float *amp, uint32_t *index);

/* 取指定 bin 的相位(rad), 从 dsp_fft_magnitude 保存的复数数据中提取 */
float dsp_find_phase(uint32_t fft_size, uint32_t bin);

#endif /* DSP_UTILS_H */
