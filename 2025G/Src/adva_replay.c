#include "adva_replay.h"
#include "adva_learn.h"     /* g_fir_table + FIR_TABLE_ROWS */
#include "dsp_utils.h"
#include "arm_math.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

/* 复用驱动 */
#include "ADS8688.h"        /* v_suoxiang[], ads8688_data_ready */
#include "stm32f4xx_hal.h"

/* printf / 屏 */
extern void UI_SendToScreen(const char *fmt, ...);

/* DAC 句柄 */
extern DAC_HandleTypeDef hdac;

/* ===== ADC 输入通道(信号发生器 → 高阻 ADC) ===== */
#define REPLAY_ADC_CH     1    /* ADS8688 CH1 = 信号发生器输入 */

/* ===== 缓冲区(复用 FFT 工作区节约 RAM) ===== */
static float32_t s_adc_buf   [DSP_FFT_SIZE];       /* ADC 采样 / FIR 输出(复用) */
static float32_t s_fir_state [DSP_FFT_SIZE + LMS_NUM_TAPS - 1]; /* FIR 状态 */
static float32_t s_fft_mag   [DSP_FFT_SIZE / 2 + 1]; /* 幅度谱 */
static uint16_t  s_dac_wave  [DSP_DAC_LENGTH];      /* DAC 整周期波形 */

/* ===== 谐波存储 ===== */
static float32_t s_harm_amp  [DSP_HARMONIC_MAX];  /* 谐波幅值 */
static float32_t s_harm_phase[DSP_HARMONIC_MAX];  /* 谐波相位 */
static uint32_t  s_harm_index;                     /* 基波 bin 号 */

/* ===== 运行状态 ===== */
static uint8_t  s_running     = 0;
static uint8_t  s_wave_type   = 0;
static uint8_t  s_last_type   = 0;
static float    s_base_freq   = 0.0f;
static float    s_last_freq   = 0.0f;
static uint32_t s_sample_cnt  = 0;
static uint8_t  s_dac_active  = 0;

/* ===== 内部声明 ===== */
static void replay_process(void);
static void synthesize_waveform(void);
static void dac_start(uint32_t freq_hz);

/* ===== 公开 API ===== */

void AdvReplay_Init(void)
{
    s_running    = 0;
    s_dac_active = 0;
    s_wave_type  = 0;
    s_last_type  = 0;
    s_base_freq  = 0.0f;
    s_last_freq  = 0.0f;
    s_sample_cnt = 0;
    memset(s_adc_buf,   0, sizeof(s_adc_buf));
    memset(s_fir_state, 0, sizeof(s_fir_state));
    memset(s_dac_wave,  0, sizeof(s_dac_wave));
}

void AdvReplay_Start(void)
{
    s_running    = 1;
    s_dac_active = 0;
    s_wave_type  = 0;
    s_last_type  = 0;
    s_last_freq  = 0.0f;
    s_sample_cnt = 0;
    ads8688_data_ready = 0;
    printf("[REPLAY] Start monitoring signal generator input (CH%d)\r\n", REPLAY_ADC_CH);
}

void AdvReplay_Stop(void)
{
    s_running = 0;
    if (s_dac_active) {
        HAL_DAC_Stop_DMA(&hdac, DAC_CHANNEL_2);
        s_dac_active = 0;
    }
    printf("[REPLAY] Stopped\r\n");
}

uint8_t AdvReplay_IsRunning(void) { return s_running; }

/* ===== 主循环调用 ===== */
void AdvReplay_Service(void)
{
    if (!s_running) return;

    /* 从 ADS8688 采集样本 */
    if (!ads8688_data_ready) return;
    ads8688_data_ready = 0;

    /* 收满一帧 FFT 点数 */
    if (s_sample_cnt < DSP_FFT_SIZE) {
        float mv = ADS8688_CodeToMilliVolt(v_suoxiang[REPLAY_ADC_CH]);
        s_adc_buf[s_sample_cnt++] = mv / 1000.0f;  /* mV→V */
    }

    if (s_sample_cnt >= DSP_FFT_SIZE) {
        s_sample_cnt = 0;
        replay_process();
    }
}

/* ===== 核心处理流水线 ===== */
static void replay_process(void)
{
    /* ① 去直流 + FFT 测频/测幅 */
    float dc = dsp_remove_dc(s_adc_buf, DSP_FFT_SIZE);
    dsp_flat_top_window(s_adc_buf, DSP_FFT_SIZE);

    /* 保存原始加窗数据(供 FIR 滤波用) */
    float32_t input_copy[DSP_FFT_SIZE];
    memcpy(input_copy, s_adc_buf, sizeof(input_copy));

    dsp_fft_magnitude(s_adc_buf, s_fft_mag, DSP_FFT_SIZE);

    float amp;
    uint32_t peak_bin;
    float freq = dsp_find_peak_freq(s_fft_mag, DSP_FFT_SIZE,
                                     DSP_SAMPLE_RATE, &amp, &peak_bin);

    /* ② 判波型(基于基波幅值, 2Vpp 源) */
    if (amp > 0.99f && amp < 1.013f) {
        s_wave_type = WAVE_SINE;
    } else if (amp < 0.85f && amp > 0.80f) {
        s_wave_type = WAVE_TRIANGLE;
    } else if (amp > 1.27f) {
        s_wave_type = WAVE_SQUARE;
    } else {
        s_wave_type = 0;  /* 未知/其他, 暂不处理 */
    }

    s_base_freq   = freq;
    s_harm_index  = peak_bin;

    printf("[REPLAY] f=%.1fHz amp=%.3fV type=%d dc=%.3f\r\n",
           freq, amp, s_wave_type, dc);

    /* ③ 频率/波型没变 → 不用重建 DAC 波形 */
    if (s_dac_active &&
        fabsf(freq - s_last_freq) < 1.0f &&
        s_wave_type == s_last_type) {
        return;
    }

    s_last_freq = freq;
    s_last_type = s_wave_type;

    /* ④ 查 FIR 表 */
    int32_t fir_idx = (int32_t)((freq - 1000.0f) / 200.0f);
    if (fir_idx < 0) fir_idx = 0;
    if (fir_idx >= (int32_t)FIR_TABLE_ROWS) fir_idx = FIR_TABLE_ROWS - 1;

    /* ⑤ FIR 滤波(输入→输出, 模拟电路) */
    arm_fir_instance_f32 fir_inst;
    memset(s_fir_state, 0, sizeof(s_fir_state));
    arm_fir_init_f32(&fir_inst, LMS_NUM_TAPS,
                     g_fir_table[fir_idx],
                     s_fir_state, DSP_FFT_SIZE);
    arm_fir_f32(&fir_inst, input_copy, s_adc_buf, DSP_FFT_SIZE);
    /* s_adc_buf 现在存放的是 FIR 输出 */

    /* ⑥ FFT 分解 FIR 输出 → 提取谐波 */
    dsp_remove_dc(s_adc_buf, DSP_FFT_SIZE);
    dsp_flat_top_window(s_adc_buf, DSP_FFT_SIZE);
    dsp_fft_magnitude(s_adc_buf, s_fft_mag, DSP_FFT_SIZE);

    /* 提取各次谐波(奇次, 1/3/5/.../19) */
    for (int h = 0; h < DSP_HARMONIC_MAX; h++) {
        uint32_t order = (uint32_t)(h * 2 + 1);  /* 1,3,5,...,19 */
        uint32_t bin   = peak_bin * order;
        if (bin >= DSP_FFT_SIZE / 2) {
            s_harm_amp[h]   = 0.0f;
            s_harm_phase[h] = 0.0f;
            continue;
        }
        s_harm_amp[h]   = s_fft_mag[bin];
        s_harm_phase[h] = dsp_find_phase(DSP_FFT_SIZE, bin);
    }
    /* 基波特殊处理(用峰值处的值) */
    s_harm_amp[0]   = s_fft_mag[peak_bin];
    s_harm_phase[0] = dsp_find_phase(DSP_FFT_SIZE, peak_bin);

    /* ⑦ 傅里叶合成整周期 → DAC 缓冲 */
    synthesize_waveform();

    /* ⑧ 启动 DAC DMA 输出 */
    dac_start((uint32_t)freq);

    UI_SendToScreen("t0.txt=\"%.2fkHz\"", freq / 1000.0f);
    UI_SendToScreen("t1.txt=\"复现中\"");
}

/* ===== 傅里叶级数合成 ===== */
static void synthesize_waveform(void)
{
    memset(s_dac_wave, 0, sizeof(s_dac_wave));
    float vref = AD603_DAC_VREF;
    float scale = 4095.0f / vref;  /* 12位 DAC */

    for (uint32_t i = 0; i < DSP_DAC_LENGTH; i++) {
        float theta = 2.0f * (float)M_PI * (float)i / (float)DSP_DAC_LENGTH;
        float signal = 1.5f;  /* DC 偏移(中点 1.5V) */

        switch (s_wave_type) {

        case WAVE_SINE:
            signal += s_harm_amp[0] * arm_sin_f32(theta + s_harm_phase[0]);
            break;

        case WAVE_TRIANGLE:
            /* 三角: 奇次谐波, ±交错, 1/n² 加权(但幅值由 FIR FFT 实测) */
            for (int h = 0; h < DSP_HARMONIC_MAX; h++) {
                float sign = (h % 2 == 0) ? 1.0f : -1.0f;
                uint32_t order = h * 2 + 1;
                signal += sign * s_harm_amp[h] *
                          arm_sin_f32(theta * (float)order + s_harm_phase[h]);
            }
            break;

        case WAVE_SQUARE:
            /* 方波: 奇次谐波, 1/n 加权(幅值由 FFT 实测) */
            for (int h = 0; h < DSP_HARMONIC_MAX; h++) {
                uint32_t order = h * 2 + 1;
                signal += s_harm_amp[h] *
                          arm_sin_f32(theta * (float)order + s_harm_phase[h]);
            }
            break;

        default:
            /* 未知波型: 直接用基波 */
            signal += s_harm_amp[0] * arm_sin_f32(theta + s_harm_phase[0]);
            break;
        }

        /* 限幅 + 量化 */
        if (signal < 0.0f) signal = 0.0f;
        if (signal > vref) signal = vref;
        s_dac_wave[i] = (uint16_t)(signal * scale);
    }
}

/* ===== DAC 输出 (TIM6 触发 + DMA 循环) ===== */
static void dac_start(uint32_t freq_hz)
{
    if (freq_hz < 100) freq_hz = 100;
    if (freq_hz > 50000) freq_hz = 50000;

    /* 停止旧输出 */
    if (s_dac_active) {
        HAL_DAC_Stop_DMA(&hdac, DAC_CHANNEL_2);
        s_dac_active = 0;
    }

    /* 配置 TIM6 触发频率: 更新率 = freq × DSP_DAC_LENGTH */
    uint32_t update_hz = freq_hz * DSP_DAC_LENGTH;
    uint32_t tim_clk   = 84000000;    /* APB1 定时器时钟 84MHz */
    uint32_t arr = tim_clk / update_hz;
    if (arr < 2) arr = 2;
    if (arr > 65535) arr = 65535;

    /* 手动配置 TIM6(若 CubeMX 未启用, 此处补偿) */
    __HAL_RCC_TIM6_CLK_ENABLE();
    TIM6->PSC = 0;
    TIM6->ARR = (uint16_t)(arr - 1);
    TIM6->CR2 |= TIM_CR2_MMS_1;   /* TRGO on update event */
    TIM6->EGR  = TIM_EGR_UG;      /* 立即加载 */
    TIM6->CR1 |= TIM_CR1_CEN;     /* 使能 */

    printf("[REPLAY] DAC start: f=%luHz arr=%lu\r\n", freq_hz, arr);

    /* DAC CH2(PA5) 配置为 TIM6 触发 + DMA */
    DAC_ChannelConfTypeDef ch_cfg = {0};
    ch_cfg.DAC_Trigger      = DAC_TRIGGER_T6_TRGO;
    ch_cfg.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
    HAL_DAC_ConfigChannel(&hdac, &ch_cfg, DAC_CHANNEL_2);

    /* 启动 DMA 循环输出 */
    HAL_DAC_Start_DMA(&hdac, DAC_CHANNEL_2,
                      (uint32_t*)s_dac_wave, DSP_DAC_LENGTH,
                      DAC_ALIGN_12B_R);
    s_dac_active = 1;
}
