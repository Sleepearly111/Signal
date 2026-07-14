#include "adva_learn.h"
#include "app_config.h"
#include "dsp_utils.h"
#include "arm_math.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

/* 复用驱动 */
#include "AD9833.h"
#include "ADS8688.h"

/* VGA / DDS 控制 */
extern void DDS_SetFrequency(uint32_t freq_hz);
extern void VGA_SetGain_Linear(float gain);

/* printf → 调试串口 */
extern void UI_SendToScreen(const char *fmt, ...);

/* ADS8688 句柄(已在 ADS8688.h 声明) */

/* ===== 学习阶段 ADC 通道(ADS8688) ===== */
#define CH_LEARN_IN   1   /* 未知电路输入端 */
#define CH_LEARN_OUT  2   /* 未知电路输出端 */

/* FFT 测量用的采样点数(100Hz / 1MHz 各一次) */
#define FFT_SAMPLE_COUNT  2048

/* ===== 全局 FIR 系数表 ===== */
float32_t g_fir_table[FIR_TABLE_ROWS][LMS_NUM_TAPS];

/* ===== 内部采样缓冲(复用) ===== */
static float32_t s_buf_in [LMS_BLOCK_SIZE];   /* 电路输入采样 */
static float32_t s_buf_out[LMS_BLOCK_SIZE];   /* 电路输出采样 */
static float32_t s_lms_work[LMS_BLOCK_SIZE];  /* LMS output / FFT workspace 复用 */

/* ===== LMS 状态(复用) ===== */
static float32_t s_lms_state[LMS_BLOCK_SIZE + LMS_NUM_TAPS - 1];
static float32_t s_lms_coeffs[LMS_NUM_TAPS];

/* 100Hz / 1MHz 两点幅值比 */
static float s_amp_ratio[2];

/* ===== 状态机 ===== */
typedef enum {
    ST_IDLE = 0,
    ST_INIT_100HZ,      /* ① 100Hz FFT 测幅值 → 存 amp_ratio[0] */
    ST_SWEEP_1,         /* ② 扫频 1k→60k@200Hz, LMS 每点学 FIR */
    ST_SWEEP_1_DONE,    /* ③ 切换到第二段扫频参数 */
    ST_SWEEP_2,         /* ④ 扫频 60k→1M@10kHz, 仅步进(不存 FIR) */
    ST_FINISH_1MHZ,     /* ⑤ 1MHz FFT 测幅值 → 存 amp_ratio[1] */
    ST_JUDGE,           /* ⑥ 两点比值判滤波类型 → 串口屏显示 */
    ST_DONE
} LearnState;

static LearnState s_state = ST_IDLE;

/* 扫频进度 */
static uint32_t s_sweep_freq;    /* 当前频率 Hz */
static uint32_t s_sweep_step;    /* 当前步进 Hz */
static uint32_t s_fir_index;     /* FIR 表写入行号 */

/* 采样计数 */
static uint32_t s_sample_count;
static uint32_t s_sample_target; /* 目标采样点数(2048 或 4096) */

/* 完成标志 */
static uint8_t s_done = 0;
static uint8_t s_filter_type = 0;

/* ===== 内部辅助 ===== */
/* 开始采集 s_sample_target 对样本(CH1+CH2) */
static void start_sampling(uint32_t count)
{
    s_sample_count  = 0;
    s_sample_target = count;
    ads8688_data_ready = 0;
}

/* 读一对样本(在 ads8688_data_ready 时调), 返回 1=本轮采集完 */
static uint8_t collect_sample(void)
{
    if (!ads8688_data_ready) return 0;
    ads8688_data_ready = 0;

    s_buf_in [s_sample_count] = ADS8688_CodeToMilliVolt(v_suoxiang[CH_LEARN_IN]);
    s_buf_out[s_sample_count] = ADS8688_CodeToMilliVolt(v_suoxiang[CH_LEARN_OUT]);
    s_sample_count++;

    return (s_sample_count >= s_sample_target) ? 1 : 0;
}

/* FFT 测幅值: 用已采集的 s_buf_in/out 前 FFT_SAMPLE_COUNT 点,存结果到 s_amp_ratio[idx] */
static void fft_measure_amp_ratio(uint32_t idx)
{
    /* 复用 s_lms_work 做 FFT mag 输出 */
    float32_t *mag_in  = s_lms_work;                  /* 输入通道幅度谱 */
    float32_t *mag_out = s_lms_work + DSP_FFT_SIZE/2 + 1; /* 输出通道幅度谱(复用后段) */

    /* 复制 + 加窗 + FFT: 电路输入侧 */
    for (uint32_t i = 0; i < DSP_FFT_SIZE; i++) {
        s_lms_work[DSP_FFT_SIZE + (DSP_FFT_SIZE/2+1) + i] = s_buf_in[i]; /* 暂存 */
    }
    /* 用 s_lms_work 后段暂存区,取前 DSP_FFT_SIZE 点 */
    float32_t *tmp = s_lms_work + DSP_FFT_SIZE + (DSP_FFT_SIZE/2+1);
    dsp_flat_top_window(tmp, DSP_FFT_SIZE);
    dsp_fft_magnitude(tmp, mag_in, DSP_FFT_SIZE);
    float amp_in;
    float freq_in = dsp_find_peak_freq(mag_in, DSP_FFT_SIZE, DSP_SAMPLE_RATE, &amp_in, NULL);

    /* 复制 + 加窗 + FFT: 电路输出侧 */
    for (uint32_t i = 0; i < DSP_FFT_SIZE; i++) {
        s_lms_work[DSP_FFT_SIZE + (DSP_FFT_SIZE/2+1) + i] = s_buf_out[i];
    }
    dsp_flat_top_window(tmp, DSP_FFT_SIZE);
    dsp_fft_magnitude(tmp, mag_out, DSP_FFT_SIZE);
    float amp_out;
    dsp_find_peak_freq(mag_out, DSP_FFT_SIZE, DSP_SAMPLE_RATE, &amp_out, NULL);

    /* 比值 = 输入/输出(>1=衰减) */
    if (amp_out > 0.001f) {
        s_amp_ratio[idx] = amp_in / amp_out;
    } else {
        s_amp_ratio[idx] = 10.0f;  /* 极小视为严重衰减 */
    }

    printf("[LEARN] amp_ratio[%lu]=%f (in=%.3fV out=%.3fV f=%.0fHz)\r\n",
           idx, s_amp_ratio[idx], amp_in, amp_out, freq_in);
}

/* 当前频率做一次 LMS → 存 FIR */
static void lms_learn_one_freq(void)
{
    /* 去直流 */
    float dc_in  = dsp_remove_dc(s_buf_in,  LMS_BLOCK_SIZE);
    float dc_out = dsp_remove_dc(s_buf_out, LMS_BLOCK_SIZE);

    /* LMS 初始化(复用 s_lms_coeffs,清零) */
    arm_lms_norm_instance_f32 lms_inst;
    memset(s_lms_coeffs, 0, sizeof(s_lms_coeffs));
    memset(s_lms_state,  0, sizeof(s_lms_state));

    arm_lms_norm_init_f32(&lms_inst, LMS_NUM_TAPS,
                          s_lms_coeffs, s_lms_state,
                          LMS_STEP_SIZE, LMS_BLOCK_SIZE);

    /* 跑一次 LMS(单块) */
    arm_lms_norm_f32(&lms_inst,
                     s_buf_in,   /* 输入 = 未知电路输入端信号 */
                     s_buf_out,  /* 参考 = 未知电路输出端信号 */
                     s_lms_work, /* 输出(不用) */
                     s_buf_in,   /* 误差(复用 s_buf_in,覆盖掉输入) */
                     LMS_BLOCK_SIZE);

    /* 保存系数 */
    memcpy(g_fir_table[s_fir_index], s_lms_coeffs, sizeof(s_lms_coeffs));
    s_fir_index++;

    printf("[LEARN] freq=%luHz fir[%lu] saved dc_in=%.3f dc_out=%.3f\r\n",
           s_sweep_freq, s_fir_index - 1, dc_in, dc_out);
}

/* ===== 公开 API ===== */

void AdvLearn_Init(void)
{
    s_state        = ST_IDLE;
    s_done         = 0;
    s_filter_type  = 0;
    s_fir_index    = 0;
    s_sweep_freq   = 0;
    memset(g_fir_table, 0, sizeof(g_fir_table));
    memset(s_lms_coeffs, 0, sizeof(s_lms_coeffs));
    memset(s_amp_ratio,  0, sizeof(s_amp_ratio));
    printf("[LEARN] AdvLearn init OK (FIR table %lu rows x %lu taps)\r\n",
           (uint32_t)FIR_TABLE_ROWS, (uint32_t)LMS_NUM_TAPS);
}

void AdvLearn_Start(void)
{
    if (s_state != ST_IDLE) return;

    s_fir_index  = 0;
    s_sweep_freq = 0;
    s_done       = 0;
    s_filter_type = 0;
    memset(s_amp_ratio, 0, sizeof(s_amp_ratio));

    /* 设 100Hz → 电路输入, VGA 固定增益 */
    AD9833_SetFrequencyQuick(100.0f, AD9833_OUT_SINUS);
    VGA_SetGain_Linear(3.33f);
    HAL_Delay(10);

    s_state = ST_INIT_100HZ;
    start_sampling(FFT_SAMPLE_COUNT);
    printf("[LEARN] Start: 100Hz FFT measurement\r\n");
}

void AdvLearn_Service(void)
{
    switch (s_state) {

    case ST_IDLE:
    case ST_DONE:
        return;

    /* ---- ① 100Hz FFT 测幅值 ---- */
    case ST_INIT_100HZ:
        if (!collect_sample()) return;
        fft_measure_amp_ratio(0);           /* amp_ratio[0] */

        /* 切换到 1kHz, 开始正式 LMS 学习 */
        s_sweep_freq = (uint32_t)SWEEP_FREQ_START;
        s_sweep_step = SWEEP_STEP_1;
        s_fir_index  = 0;
        AD9833_SetFrequencyQuick((float)s_sweep_freq, AD9833_OUT_SINUS);
        HAL_Delay(2);
        s_state = ST_SWEEP_1;
        start_sampling(LMS_BLOCK_SIZE);
        printf("[LEARN] Sweep-1 start: %luHz step %luHz\r\n",
               s_sweep_freq, (uint32_t)s_sweep_step);
        return;

    /* ---- ② 扫频 1k→60k@200Hz ---- */
    case ST_SWEEP_1:
        if (!collect_sample()) return;

        /* 当前频点 LMS 学习 → 存 FIR */
        lms_learn_one_freq();

        s_sweep_freq += s_sweep_step;
        if (s_sweep_freq > (uint32_t)SWEEP_FREQ_1_END) {
            s_state = ST_SWEEP_1_DONE;
            printf("[LEARN] Sweep-1 complete (%lu FIR rows)\r\n", s_fir_index);
            return;
        }

        AD9833_SetFrequencyQuick((float)s_sweep_freq, AD9833_OUT_SINUS);
        start_sampling(LMS_BLOCK_SIZE);
        return;

    /* ---- ③ 切换第二段 ---- */
    case ST_SWEEP_1_DONE:
        s_sweep_freq = (uint32_t)SWEEP_FREQ_2_START;
        s_sweep_step = SWEEP_STEP_2;
        AD9833_SetFrequencyQuick((float)s_sweep_freq, AD9833_OUT_SINUS);
        s_state = ST_SWEEP_2;
        printf("[LEARN] Sweep-2 start: %luHz step %luHz (no FIR)\r\n",
               s_sweep_freq, (uint32_t)s_sweep_step);
        return;

    /* ---- ④ 扫频 60k→1M@10kHz(仅步进) ---- */
    case ST_SWEEP_2: {
        /* 快速步进,不采样不 LMS */
        s_sweep_freq += s_sweep_step;
        if (s_sweep_freq > (uint32_t)SWEEP_FREQ_2_END) {
            /* 到达 1MHz → FFT 测幅值 */
            AD9833_SetFrequencyQuick(1000000.0f, AD9833_OUT_SINUS);
            HAL_Delay(2);
            s_state = ST_FINISH_1MHZ;
            start_sampling(FFT_SAMPLE_COUNT);
            printf("[LEARN] Sweep-2 done, 1MHz FFT\r\n");
            return;
        }
        AD9833_SetFrequencyQuick((float)s_sweep_freq, AD9833_OUT_SINUS);
        HAL_Delay(1);  /* 每步 1ms,94步≈94ms */
        return;
    }

    /* ---- ⑤ 1MHz FFT 测幅值 ---- */
    case ST_FINISH_1MHZ:
        if (!collect_sample()) return;
        fft_measure_amp_ratio(1);           /* amp_ratio[1] */
        s_state = ST_JUDGE;
        return;

    /* ---- ⑥ 判类型 ---- */
    case ST_JUDGE: {
        float r0 = s_amp_ratio[0];   /* 100Hz */
        float r1 = s_amp_ratio[1];   /* 1MHz */

        /* 比值≈1 ↔ 输出≈输入 ↔ 通带; 比值≫1 ↔ 衰减 ↔ 阻带 */
        uint8_t pass_100 = (fabsf(r0 - 1.0f) < 0.3f) ? 1 : 0;
        uint8_t pass_1M  = (fabsf(r1 - 1.0f) < 0.3f) ? 1 : 0;

        if      ( pass_100 && !pass_1M) s_filter_type = FILTER_LOW_PASS;
        else if (!pass_100 &&  pass_1M) s_filter_type = FILTER_HIGH_PASS;
        else if (!pass_100 && !pass_1M) s_filter_type = FILTER_BAND_PASS;
        else if ( pass_100 &&  pass_1M) s_filter_type = FILTER_BAND_STOP;
        else                             s_filter_type = 0;

        const char *names[] = {"???", "低通", "高通", "带通", "带阻"};
        printf("[LEARN] Type=%s (r100=%.3f r1M=%.3f)\r\n",
               names[s_filter_type], r0, r1);
        UI_SendToScreen("rec.txt=\"%d\"", s_filter_type);
        UI_SendToScreen("t.txt=\"学习完成\"");

        /* 复位扫频参数, 避免下次调用残留 */
        s_sweep_freq = (uint32_t)SWEEP_FREQ_START;
        s_sweep_step = SWEEP_STEP_1;

        s_done  = 1;
        s_state = ST_DONE;
        printf("[LEARN] === 学习完成 ===\r\n");
        return;
    }

    default:
        break;
    }
}

uint8_t AdvLearn_IsDone(void)
{
    return s_done;
}

uint8_t AdvLearn_GetFilterType(void)
{
    return s_filter_type;
}
