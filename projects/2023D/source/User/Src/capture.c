#include "capture.h"
#include "app_config.h"
#include "adc.h"
#include "tim.h"
#include "hmi.h"
#include <string.h>

/* ---- 外部 HAL 句柄 ------------------------------------------- */
extern DMA_HandleTypeDef hdma_tim8_ch1;

/* ---- 缓冲区 -------------------------------------------------- */
static uint16_t env_buf[ENV_SAMPLES];
static uint16_t cap_buf[CAP_SAMPLES];

/* ---- ISR → 主循环 传递 -------------------------------------- */
static volatile bool window_end_pending;  /* ISR 通知: 窗口时间到    */
static volatile uint16_t cap_remaining;   /* ISR 快照: NDTR 剩余计数  */
static volatile bool cap_buffer_full;     /* TIM8 DMA 提前装满        */

/* ---- 主循环状态 ---------------------------------------------- */
static bool window_done;                  /* 窗口结束且统计完成       */
static bool timed_out;                    /* 300ms 超时               */
static CaptureExecStatus_t exec_status;
static CaptureQuality_t    cap_quality;
static CaptureCapStats_t   cap_stats;
static uint32_t t_start;

/* ---- 内部辅助: 统计计算 (主循环调用, 不在 ISR) --------------- */
static void compute_cap_stats(int cap_count)
{
    cap_stats.cap_count        = cap_count;
    cap_stats.expected_count   = CAP_EXPECTED_COUNT;
    cap_stats.capture_ratio    = (CAP_EXPECTED_COUNT > 0)
        ? (float)cap_count / (float)CAP_EXPECTED_COUNT : 0.0f;
    cap_stats.cap_buffer_full  = cap_buffer_full;
    cap_stats.zero_intervals   = 0;
    cap_stats.short_intervals  = 0;
    cap_stats.long_intervals   = 0;
    cap_stats.normal_intervals = 0;
    cap_stats.min_ticks        = 0xFFFFU;
    cap_stats.max_ticks        = 0;

    if (cap_count < 2) {
        cap_stats.min_ticks = 0;
        return;
    }

    for (int i = 0; i < cap_count - 1; i++) {
        uint16_t d = (uint16_t)(cap_buf[i + 1] - cap_buf[i]);

        if (d == 0) {
            cap_stats.zero_intervals++;
            continue;
        }

        if (d < CAP_MIN_CARRIER_TICKS)
            cap_stats.short_intervals++;
        else if (d > CAP_MAX_INTERVAL_TICKS)
            cap_stats.long_intervals++;
        else
            cap_stats.normal_intervals++;

        if (d < cap_stats.min_ticks) cap_stats.min_ticks = d;
        if (d > cap_stats.max_ticks) cap_stats.max_ticks = d;
    }

    if (cap_stats.min_ticks == 0xFFFFU)
        cap_stats.min_ticks = 0;
}

/* ---- 内部辅助: 质量判定 (主循环调用) ------------------------- */
static void compute_cap_quality(int cap_count)
{
    if (exec_status == CAPTURE_HAL_ERROR) {
        cap_quality = CAP_QUALITY_INVALID;
        return;
    }

    /* EMPTY 必须在 TRUNCATED 之前: cap=0 时即使 flag 误置也不应判截断 */
    if (cap_count == 0) {
        cap_quality = CAP_QUALITY_EMPTY;
        return;
    }

    if (cap_buffer_full) {
        cap_quality = CAP_QUALITY_TRUNCATED;
        return;
    }

    /* CONTINUOUS 需要同时满足 (保守诊断条件, 待硬件数据标定):
     *   1. capture_ratio 在 [0.90, 1.05] — 捕获率接近连续载波预期
     *   2. zero_intervals == 0 — 无计数器/比较器异常
     *   3. long_intervals <= 2 — 最多容忍2个异常长间隔
     *
     * 不满足任一条件 → PARTIAL (频率路不可完全信赖)
     */
    float ratio  = cap_stats.capture_ratio;
    bool ratio_ok = (ratio >= 0.90f && ratio <= 1.05f);
    bool long_ok  = (cap_stats.long_intervals <= 2);
    bool zero_ok  = (cap_stats.zero_intervals == 0);

    if (ratio_ok && long_ok && zero_ok) {
        cap_quality = CAP_QUALITY_CONTINUOUS;
    } else {
        cap_quality = CAP_QUALITY_PARTIAL;
    }
}

/* ---- 公开 API ------------------------------------------------ */

void Capture_Init(void)
{
    __HAL_TIM_SET_PRESCALER(&htim3, 0);
    __HAL_TIM_SET_AUTORELOAD(&htim3, TIM3_CLK_HZ / ENV_SAMPLE_RATE - 1);
}

/**
 * 启动顺序 (记录于文档):
 *   1. TIM8 输入捕获 DMA (先开, 避免漏频率路开头)
 *   2. ADC DMA
 *   3. TIM3 (开始 200kHz TRGO → 触发 ADC)
 *
 * 同步偏差: TIM8 比 ADC 窗口提前约 3 个 HAL 调用 (~<1us),
 * 在 40ms 尺度上可忽略。两路共享 ADC 完成时刻作为统一结束点。
 */
void Capture_Start(void)
{
    /* ---- 清零主循环状态 ---- */
    window_done         = false;
    timed_out           = false;
    cap_remaining       = 0;
    cap_buffer_full     = false;
    window_end_pending  = false;
    exec_status         = CAPTURE_WINDOW_OK;
    cap_quality         = CAP_QUALITY_EMPTY;
    t_start             = HAL_GetTick();

    memset(&cap_stats, 0, sizeof(cap_stats));

    /* Step 1: TIM8 输入捕获 DMA */
    if (HAL_TIM_IC_Start_DMA(&htim8, TIM_CHANNEL_1,
                             (uint32_t *)cap_buf, CAP_SAMPLES) != HAL_OK) {
        exec_status = CAPTURE_HAL_ERROR;
        cap_quality = CAP_QUALITY_INVALID;
        window_done = true;
        return;
    }

    /* Step 2: ADC DMA */
    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)env_buf, ENV_SAMPLES) != HAL_OK) {
        HAL_TIM_IC_Stop_DMA(&htim8, TIM_CHANNEL_1);
        exec_status = CAPTURE_HAL_ERROR;
        cap_quality = CAP_QUALITY_INVALID;
        window_done = true;
        return;
    }

    /* Step 3: TIM3 (200kHz TRGO) */
    if (HAL_TIM_Base_Start(&htim3) != HAL_OK) {
        HAL_ADC_Stop_DMA(&hadc1);
        HAL_TIM_IC_Stop_DMA(&htim8, TIM_CHANNEL_1);
        exec_status = CAPTURE_HAL_ERROR;
        cap_quality = CAP_QUALITY_INVALID;
        window_done = true;
        return;
    }
}

/**
 * 主循环轮询。
 * 正常完成路径: ISR 设 window_end_pending → 本函数完成统计 → window_done
 * 超时路径: 300ms 未完成 → 抢救 + 统计
 */
void Capture_Service(void)
{
    /* ---- 已完成的窗口不再处理 ---- */
    if (window_done) return;

    /* ---- 超时检测 ---- */
    if (!timed_out && !window_end_pending
        && HAL_GetTick() - t_start > CAPTURE_TIMEOUT_MS) {
        timed_out = true;

        cap_remaining = (uint16_t)__HAL_DMA_GET_COUNTER(&hdma_tim8_ch1);

        HAL_TIM_Base_Stop(&htim3);
        HAL_ADC_Stop_DMA(&hadc1);
        HAL_TIM_IC_Stop_DMA(&htim8, TIM_CHANNEL_1);

        int cap_count = CAP_SAMPLES - (int)cap_remaining;
        if (cap_count < 0) cap_count = 0;

        exec_status = CAPTURE_TIMEOUT;
        compute_cap_stats(cap_count);
        compute_cap_quality(cap_count);
        window_done = true;

        dbg_printf("[capture] TIMEOUT %lums cap=%d qual=%d\r\n",
                   CAPTURE_TIMEOUT_MS, cap_count, cap_quality);
        return;
    }

    /* ---- 窗口结束 (ISR 已冻结硬件, 主循环做统计) ---- */
    if (!window_end_pending) return;

    /* 主循环 HAL 停止 (不在 ISR 中执行) */
    HAL_ADC_Stop_DMA(&hadc1);
    HAL_TIM_IC_Stop_DMA(&htim8, TIM_CHANNEL_1);

    int cap_count = CAP_SAMPLES - (int)cap_remaining;
    if (cap_count < 0) cap_count = 0;
    if (cap_count > CAP_SAMPLES) cap_count = CAP_SAMPLES;

    exec_status = CAPTURE_WINDOW_OK;
    compute_cap_stats(cap_count);
    compute_cap_quality(cap_count);
    window_done         = true;
    window_end_pending  = false;
}

/* ---- 状态查询 ----------------------------------------------- */

bool             Capture_IsDone(void)       { return window_done; }
bool             Capture_TimedOut(void)     { return timed_out; }
CaptureExecStatus_t Capture_GetExecStatus(void)  { return exec_status; }
CaptureQuality_t Capture_GetQuality(void)       { return cap_quality; }

const CaptureCapStats_t *Capture_GetCapStats(void) { return &cap_stats; }

/* ---- 数据访问 ----------------------------------------------- */

const uint16_t *Capture_GetEnvelope(int *n) { *n = ENV_SAMPLES; return env_buf; }

const uint16_t *Capture_GetCaptures(int *n_cap)
{
    int cnt = CAP_SAMPLES - (int)cap_remaining;
    if (cnt < 0) cnt = 0;
    if (cnt > CAP_SAMPLES) cnt = CAP_SAMPLES;
    *n_cap = cnt;
    return cap_buf;
}

/* ================================================================
 * HAL 回调 — ISR 上下文
 *
 * ADC 完成回调 (DMA 中断内):
 *   只做硬件冻结 + 置 flag, 不在 ISR 中遍历 cap_buf 或做统计。
 *
 * 修改前 ISR 工作量:
 *   HAL_TIM_Base_Stop + compute_cap_stats (遍历 ~10000 点)
 *   ≈ 数十万周期 (计算量取决于 cap_count)
 *
 * 修改后 ISR 工作量:
 *   HAL_TIM_Base_Stop + NDTR 读取 + TIM8 disable + DMA disable + flag
 *   ≈ 数十周期 (纯寄存器操作)
 * ================================================================ */

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc != &hadc1) return;

    /* ① 停止 TIM3 (不再产生 ADC 触发) — 寄存器写, ISR-safe */
    HAL_TIM_Base_Stop(&htim3);

    /* ② 快照 TIM8 DMA 剩余计数 (停止前读) */
    cap_remaining = (uint16_t)__HAL_DMA_GET_COUNTER(&hdma_tim8_ch1);

    /* ③ 冻结 TIM8 捕获 + DMA (寄存器写, 防止更多数据写入 cap_buf) */
    __HAL_TIM_DISABLE(&htim8);
    __HAL_DMA_DISABLE(&hdma_tim8_ch1);

    /* ④ 通知主循环 (不在此处做统计计算) */
    window_end_pending = true;
}

/**
 * TIM8 DMA 传输完成 (提前装满缓冲)。
 * 仅记录截断标志, 统计由主循环在 window_end_pending 后统一计算。
 */
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    if (htim == &htim8) cap_buffer_full = true;
}
