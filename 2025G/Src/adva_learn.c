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
#define CH_LEARN_IN   0   /* 未知电路输入端(装置输出) */
#define CH_LEARN_OUT  1   /* 未知电路输出端 */

/* FFT 测量用的采样点数(100Hz / 1MHz 各一次) */
#define FFT_SAMPLE_COUNT  1024

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
static float s_amp_ratio[2];  /* 100Hz, 30kHz */

/* ===== 状态机(仅判断,暂不学习) ===== */
typedef enum {
    ST_IDLE = 0,
    ST_MEASURE_LOW,     /* ① 100Hz Vpp 测比(低频参考) */
    ST_MEASURE_HIGH,    /* ② 30kHz Vpp 测比(高频参考) */
    ST_JUDGE,           /* ③ 两点比值判滤波类型 */
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

    uint16_t raw0 = v_suoxiang[0];  /* CH0(丝印CH1) = 装置输出 */
    uint16_t raw1 = v_suoxiang[1];  /* CH1(丝印CH2) = 电路输出 */
    s_buf_in [s_sample_count] = ADS8688_CodeToMilliVolt(raw0);
    s_buf_out[s_sample_count] = ADS8688_CodeToMilliVolt(raw1);

    if (s_sample_count < 5) {
        printf("[LEARN] sample[%lu] raw0=%u raw1=%u → in=%.0fmV out=%.0fmV\r\n",
               s_sample_count, raw0, raw1,
               s_buf_in[s_sample_count], s_buf_out[s_sample_count]);
    }
    s_sample_count++;

    return (s_sample_count >= s_sample_target) ? 1 : 0;
}

/* 切换自动扫描到单通道, 用于高频测量避免交替欠采样 */
static void set_auto_seq_single_ch(uint8_t ch)
{
    ADS8688_BeginAccess();
    ADS8688A_Write_Program_Register(0x01U, (uint8_t)(1U << ch));  /* 只开 ch */
    AUTO_RST_Mode();
    ADS8688_EndAccess();
}

/* 恢复 CH0+CH1 双通道自动扫描 */
static void set_auto_seq_dual(void)
{
    ADS8688_BeginAccess();
    ADS8688A_Write_Program_Register(0x01U, 0x03U);  /* CH0+CH1 */
    AUTO_RST_Mode();
    ADS8688_EndAccess();
}

/* 单通道采集: 只读 v_suoxiang[0], 存到 dst */
static uint8_t collect_single_ch(float32_t *dst)
{
    if (!ads8688_data_ready) return 0;
    ads8688_data_ready = 0;
    dst[s_sample_count] = ADS8688_CodeToMilliVolt(v_suoxiang[0]);
    s_sample_count++;
    return (s_sample_count >= s_sample_target) ? 1 : 0;
}

/* Vpp 法测幅值比: 直接算峰峰值(不依赖 FFT), 存到 s_amp_ratio[idx] */
static void vpp_measure_ratio(uint32_t idx, uint32_t count)
{
    float min_in  = s_buf_in[0],  max_in  = s_buf_in[0];
    float min_out = s_buf_out[0], max_out = s_buf_out[0];

    for (uint32_t i = 1; i < count; i++) {
        if (s_buf_in[i]  < min_in)  min_in  = s_buf_in[i];
        if (s_buf_in[i]  > max_in)  max_in  = s_buf_in[i];
        if (s_buf_out[i] < min_out) min_out = s_buf_out[i];
        if (s_buf_out[i] > max_out) max_out = s_buf_out[i];
    }

    float vpp_in  = max_in  - min_in;
    float vpp_out = max_out - min_out;

    if (vpp_out > 5.0f) {    /* 输出 >5mV 认为有效 */
        s_amp_ratio[idx] = vpp_in / vpp_out;
    } else {
        s_amp_ratio[idx] = 200.0f;  /* 输出极小 → 强衰减(阻带) */
    }

    printf("[LEARN] ratio[%lu]=%.3f (vpp_in=%.1fmV vpp_out=%.1fmV dc_in=%.0fmV dc_out=%.0fmV)\r\n",
           idx, s_amp_ratio[idx], vpp_in, vpp_out,
           (max_in+min_in)/2.0f, (max_out+min_out)/2.0f);
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

    /* 设 100Hz → 电路输入(低频通带参考) */
    AD9833_SetFrequencyQuick(100.0f, AD9833_OUT_SINUS);
    VGA_SetGain_Linear(0.5f);
    HAL_Delay(10);

    s_state = ST_MEASURE_LOW;
    start_sampling(FFT_SAMPLE_COUNT);
    printf("[LEARN] Start: 100Hz measurement\r\n");
}

void AdvLearn_Service(void)
{
    switch (s_state) {

    case ST_IDLE:
    case ST_DONE:
        return;

    /* ---- ① 100Hz Vpp 测幅比(低频通带) ---- */
    case ST_MEASURE_LOW:
        if (!collect_sample()) return;
        vpp_measure_ratio(0, s_sample_target);

        /* → 30kHz 高频测量 */
        AD9833_SetFrequencyQuick(30000.0f, AD9833_OUT_SINUS);
        HAL_Delay(5);
        s_state = ST_MEASURE_HIGH;
        start_sampling(FFT_SAMPLE_COUNT);
        return;

    /* ---- ② 30kHz Vpp 测幅比(高频阻带) ---- */
    case ST_MEASURE_HIGH:
        if (!collect_sample()) return;
        vpp_measure_ratio(1, s_sample_target);
        s_state = ST_JUDGE;
        return;

    /* ---- ③ 判类型(两点法) ---- */
    case ST_JUDGE: {
        float r_low  = s_amp_ratio[0];  /* 100Hz */
        float r_high = s_amp_ratio[1];  /* 30kHz */

        printf("[JUDGE] ========== 判定详情 ==========\r\n");
        printf("[JUDGE] 100Hz: ratio=%.2f → %s\r\n", r_low,  (r_low<2.0f)?"通":"阻");
        printf("[JUDGE] 30kHz: ratio=%.2f → %s\r\n", r_high, (r_high<2.0f)?"通":"阻");

        uint8_t pass_low  = (r_low  < 2.0f) ? 1 : 0;
        uint8_t pass_high = (r_high < 2.0f) ? 1 : 0;

        if      ( pass_low && !pass_high) s_filter_type = FILTER_LOW_PASS;
        else if (!pass_low &&  pass_high) s_filter_type = FILTER_HIGH_PASS;
        else if (!pass_low && !pass_high) s_filter_type = FILTER_BAND_PASS;
        else                               s_filter_type = FILTER_BAND_STOP;

        const char *names[] = {"???", "低通", "高通", "带通", "带阻"};
        printf("[JUDGE] 模式: low=%d high=%d\r\n", pass_low, pass_high);
        printf("[JUDGE] === 结果: %s ===\r\n", names[s_filter_type]);
        UI_SendToScreen("rec.txt=\"%d\"", s_filter_type);
        UI_SendToScreen("t.txt=\"%s\"", names[s_filter_type]);

        s_done  = 1;
        s_state = ST_DONE;
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
