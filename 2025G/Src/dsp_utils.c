#include "dsp_utils.h"
#include "app_config.h"
#include "arm_math.h"
#include <math.h>
#include <stdlib.h>

/* ===== 内部状态 ===== */
static arm_rfft_fast_instance_f32 g_rfft_inst;
static float32_t g_fft_complex[DSP_FFT_SIZE];   /* RFFT interleaved 复数输出 */
static uint8_t  g_dsp_init_done = 0;

/* Flat-Top 5项余弦窗系数 (HP/Agilent 标准) */
#define FT_A0  0.21557895f
#define FT_A1  0.41663158f
#define FT_A2  0.277263158f
#define FT_A3  0.083578947f
#define FT_A4  0.006947368f
#define FT_COHERENT_GAIN  FT_A0   /* 相干增益 = a0 */

/* ===== 一次性初始化 ===== */
void dsp_init(void)
{
    if (g_dsp_init_done) return;
    arm_status st = arm_rfft_fast_init_f32(&g_rfft_inst, DSP_FFT_SIZE);
    if (st != ARM_MATH_SUCCESS) {
        /* 初始化失败——打印错误但继续,后续操作静默跳过 */
        return;
    }
    g_dsp_init_done = 1;
}

/* ===== 去直流 ===== */
float dsp_remove_dc(float32_t *data, uint32_t len)
{
    float dc;
    arm_mean_f32(data, len, &dc);
    for (uint32_t i = 0; i < len; i++) {
        data[i] -= dc;
    }
    return dc;
}

/* ===== Flat-Top 平顶窗(就地) ===== */
void dsp_flat_top_window(float32_t *data, uint32_t len)
{
    if (len < 2) return;
    float nf = (float)(len - 1);
    for (uint32_t i = 0; i < len; i++) {
        float x = 2.0f * (float)M_PI * (float)i / nf;
        float w = FT_A0
                - FT_A1 * cosf(x)
                + FT_A2 * cosf(2.0f * x)
                - FT_A3 * cosf(3.0f * x)
                + FT_A4 * cosf(4.0f * x);
        data[i] *= w;
    }
}

/* ===== RFFT → 归一化幅度谱 ===== */
void dsp_fft_magnitude(float32_t *data, float32_t *mag, uint32_t fft_size)
{
    if (!g_dsp_init_done) return;

    /* ① 实数 FFT */
    arm_rfft_fast_f32(&g_rfft_inst, data, g_fft_complex, 0);

    uint32_t nyq = fft_size / 2;
    float norm = 1.0f / ((float)fft_size * FT_COHERENT_GAIN);

    /* ② DC (bin 0) */
    mag[0] = fabsf(g_fft_complex[0]) * norm;

    /* ③ bins 1 .. N/2-1 */
    for (uint32_t k = 1; k < nyq; k++) {
        float re = g_fft_complex[2 * k];
        float im = g_fft_complex[2 * k + 1];
        /* ×2 补偿实信号能量在正负频率各半 */
        mag[k] = sqrtf(re * re + im * im) * 2.0f * norm;
    }

    /* ④ Nyquist (bin N/2) */
    mag[nyq] = fabsf(g_fft_complex[1]) * norm;
}

/* ===== 获取内部 FFT 复数数据(用于相位提取) ===== */
const float32_t* dsp_get_fft_complex(void)
{
    return g_fft_complex;
}

/* ===== 在幅度谱中找基波峰 → 频率(抛物线插值) ===== */
float dsp_find_peak_freq(const float32_t *mag, uint32_t fft_size,
                         float sample_rate, float *amp, uint32_t *index)
{
    uint32_t nyq   = fft_size / 2;
    uint32_t min_b = 4;            /* 跳过低频噪声(DC~约500Hz),从 bin 4 开始搜 */
    if (min_b >= nyq) min_b = 1;

    /* 找幅度最大的 bin */
    uint32_t peak_bin = min_b;
    float    peak_val = mag[min_b];
    for (uint32_t k = min_b + 1; k < nyq; k++) {
        if (mag[k] > peak_val) {
            peak_val = mag[k];
            peak_bin = k;
        }
    }

    /* 抛物线插值(3点), 修正频率估计 */
    float delta = 0.0f;
    if (peak_bin > min_b && peak_bin < nyq - 1) {
        float y0 = mag[peak_bin - 1];
        float y1 = mag[peak_bin];
        float y2 = mag[peak_bin + 1];
        float denom = 2.0f * (2.0f * y1 - y0 - y2);
        if (fabsf(denom) > 1e-12f) {
            delta = (y0 - y2) / denom;
        }
    }

    float freq = ((float)peak_bin + delta) * sample_rate / (float)fft_size;

    /* 插值修正幅值 */
    float p_amp = peak_val;
    if (fabsf(delta) < 0.5f) {
        p_amp = peak_val / (1.0f - delta * delta);  /* 抛物线修正 */
    }

    if (amp)   *amp   = p_amp;
    if (index) *index = peak_bin;

    return freq;
}

/* ===== 取指定 bin 的相位 ===== */
float dsp_find_phase(uint32_t fft_size, uint32_t bin)
{
    if (!g_dsp_init_done) return 0.0f;

    uint32_t nyq = fft_size / 2;

    if (bin == 0) {
        /* DC: 纯实数, 相位为0或π */
        return (g_fft_complex[0] >= 0.0f) ? 0.0f : (float)M_PI;
    }
    if (bin == nyq) {
        /* Nyquist: 纯实数 */
        return (g_fft_complex[1] >= 0.0f) ? 0.0f : (float)M_PI;
    }

    float re = g_fft_complex[2 * bin];
    float im = g_fft_complex[2 * bin + 1];
    return atan2f(im, re);
}
