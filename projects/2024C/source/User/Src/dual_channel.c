/**
 * @file    dual_channel.c
 * @brief   2024C 双通道 DDS 引擎 —— 实现
 *
 * 为 SD (直达) 和 SM (多径) 两通道生成数字波形。
 * CPU 在参数改变时用 sinf() 计算 1000 点 DMA 缓冲，
 * DMA 循环搬运到 DAC1(PA4) / DAC2(PA5)。
 *
 * 双缓冲 + 统一停机同步重启方案：
 *   1. 新参数写入后台缓冲 (buf_*_B)
 *   2. 停 TIM6 → 停两路 DMA
 *   3. 交换前后台 → 两路 DMA 同时重启 → TIM6 同时重启
 *   4. 两通道从第 0 样本起同步输出
 *
 * 信号模型:
 *   SD[n] = Ac × [1 + m×sin(2π×fm×n/fs)] × sin(2π×fc×n/fs)
 *   SM[n] = α×Ac × [1 + m×sin(2π×fm×(n/fs - τ))] × sin(2π×fc×(n/fs - τ) + φ)
 */

#include "dual_channel.h"
#include "app_config.h"
#include "dac.h"
#include "tim.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

/* ================================================================
 * 外部 HAL 句柄 (CubeMX 生成于 Core/Src/)
 * ================================================================ */

extern DAC_HandleTypeDef hdac;
extern TIM_HandleTypeDef htim6;

/* ================================================================
 * A/B 双缓冲: 每通道两块，每块 BUFFER_SIZE 个 uint16_t
 * ================================================================ */

static uint16_t buf_SD_A[BUFFER_SIZE];
static uint16_t buf_SD_B[BUFFER_SIZE];
static uint16_t buf_SM_A[BUFFER_SIZE];
static uint16_t buf_SM_B[BUFFER_SIZE];

static uint16_t *fb_SD = NULL;   /* 前台 (DMA 正在读) */
static uint16_t *bb_SD = NULL;   /* 后台 (CPU 可写) */
static uint16_t *fb_SM = NULL;
static uint16_t *bb_SM = NULL;

static int dma_running = 0;      /* 0=未启动, 1=运行中 */

/* ================================================================
 * 当前参数
 * ================================================================ */

static DualChannel_Params current_params;

/* ================================================================
 * 内部工具函数
 * ================================================================ */

/* 将角度归一化到 [0, 360) */
static float wrap360(float deg) {
    float r = fmodf(deg, 360.0f);
    if (r < 0.0f) r += 360.0f;
    return r;
}

/* CLAMP 宏 */
#define CLAMP(v, lo, hi) (((v) < (lo)) ? (lo) : ((v) > (hi)) ? (hi) : (v))

/**
 * @brief 用 sinf() 计算整个后台缓冲区
 * @param params  目标参数
 * @retval 0=成功, -1=码值越安全边界
 */
static int compute_buffers(const DualChannel_Params *p)
{
    /* ---- 32-bit DDS 相位步进 ---- */
    uint32_t phase_inc = (uint32_t)((double)p->fc_hz / SAMPLE_RATE_HZ * 4294967296.0);

    /* ---- 时延分解 ---- */
    int    delay_n   = (int)(p->tau_us);     /* 1Msps = 1样本/μs, 恰好整数 */
    float  phi_tau   = p->fc_hz * (float)p->tau_us * 360.0e-6f; /* 时延→载波相位 */
    float  phi_total = wrap360(p->phi_deg - phi_tau);
    uint32_t phi_word = (uint32_t)((double)phi_total / 360.0 * 4294967296.0);

    /* ---- 幅度系数 ---- */
    float amp_fullscale = (float)DAC_CENTER - (float)DAC_CODE_MIN_SAFE;
    /* SD 幅度:  Vpp_out = Vrms × 2√2,  Vpp_dac = Vpp_out / gain */
    float dac_vpp_SD = (float)p->vrms_mv * 0.001f * RMS_TO_VPP / THS4001_GAIN;
    float amp_SD = dac_vpp_SD / DAC_FULLSCALE_VPP;

    float att_linear = powf(10.0f, -p->atten_db / 20.0f);
    float amp_SM = amp_SD * att_linear;

    /* AM 包络的每样本弧度 */
    float am_w = 2.0f * PI * (float)AM_FREQ_HZ / (float)SAMPLE_RATE_HZ;

    int clipping = 0;

    for (uint32_t n = 0; n < BUFFER_SIZE; n++) {
        /* 载波相位 (弧度) — DDS 32-bit 相位累加器, 取低 32 位 */
        uint32_t accum_sd = (uint32_t)(phase_inc * (uint64_t)n);
        float phase_sd = 2.0f * PI * (float)accum_sd / 4294967296.0f;
        /* SM 相位 = SD 相位 + 偏移 */
        uint64_t sm_acc = (uint64_t)phase_inc * n + phi_word;
        uint32_t accum_sm = (uint32_t)sm_acc;
        float phase_sm = 2.0f * PI * (float)accum_sm / 4294967296.0f;

        /* AM 包络 */
        float env_sd = 1.0f;
        float env_sm = 1.0f;
        if (!p->is_cw) {
            env_sd = 1.0f + p->ma * sinf(am_w * (float)n);
            /* SM 包络晚 delay_n 个样本 */
            int idx_sm = (int)n - delay_n;
            if (idx_sm < 0) idx_sm += (int)BUFFER_SIZE; /* 回绕 */
            env_sm = 1.0f + p->ma * sinf(am_w * (float)idx_sm);
        }

        /* 合成 DAC 码值 */
        float raw_sd = (float)DAC_CENTER + amp_SD * amp_fullscale * sinf(phase_sd) * env_sd;
        float raw_sm = (float)DAC_CENTER + amp_SM * amp_fullscale * sinf(phase_sm) * env_sm;

        /* 安全范围检查 */
        if (raw_sd < (float)DAC_CODE_MIN_SAFE || raw_sd > (float)DAC_CODE_MAX_SAFE ||
            raw_sm < (float)DAC_CODE_MIN_SAFE || raw_sm > (float)DAC_CODE_MAX_SAFE) {
            clipping = -1;
        }

        /* CLAMP 为最终硬件保护，写入后台缓冲 */
        bb_SD[n] = (uint16_t)(CLAMP(raw_sd, 0.0f, 4095.0f) + 0.5f);
        bb_SM[n] = (uint16_t)(CLAMP(raw_sm, 0.0f, 4095.0f) + 0.5f);
    }

    return clipping;
}

/**
 * @brief 统一停机 → 交换前后台 → 同步重启
 */
static void swap_and_restart(void)
{
    /* 1. 停 TIM6 (停止触发) */
    HAL_TIM_Base_Stop(&htim6);

    /* 2. 停两路 DAC DMA */
    HAL_DAC_Stop_DMA(&hdac, DAC_CHANNEL_1);
    HAL_DAC_Stop_DMA(&hdac, DAC_CHANNEL_2);

    /* 3. 交换前后台 */
    uint16_t *tmp = fb_SD;
    fb_SD = bb_SD;
    bb_SD = tmp;

    tmp = fb_SM;
    fb_SM = bb_SM;
    bb_SM = tmp;

    /* 4. 两路 DAC+DMA 同时启动 */
    HAL_DAC_Start_DMA(&hdac, DAC_CHANNEL_1, (uint32_t *)fb_SD, BUFFER_SIZE, DAC_ALIGN_12B_R);
    HAL_DAC_Start_DMA(&hdac, DAC_CHANNEL_2, (uint32_t *)fb_SM, BUFFER_SIZE, DAC_ALIGN_12B_R);

    /* 5. 启动 TIM6 → 两通道从样本 0 起同步输出 */
    HAL_TIM_Base_Start(&htim6);
}

/* ================================================================
 * 公共接口
 * ================================================================ */

int DualChannel_Init(const DualChannel_Params *params)
{
    /* 默认参数 */
    DualChannel_Params defaults = {
        .fc_hz    = DEFAULT_FC_HZ,
        .is_cw    = DEFAULT_MODE_CW,
        .ma       = DEFAULT_MA,
        .vrms_mv  = DEFAULT_VRMS_MV,
        .tau_us   = DEFAULT_TAU_US,
        .atten_db = DEFAULT_ATTEN_DB,
        .phi_deg  = DEFAULT_PHI_DEG,
    };

    if (params != NULL) {
        memcpy(&current_params, params, sizeof(DualChannel_Params));
    } else {
        memcpy(&current_params, &defaults, sizeof(DualChannel_Params));
    }

    /* 初始: A = 前台, B = 后台 */
    fb_SD = buf_SD_A;
    bb_SD = buf_SD_B;
    fb_SM = buf_SM_A;
    bb_SM = buf_SM_B;

    /* 首次计算 → 后台缓冲 */
    int ret = compute_buffers(&current_params);
    if (ret != 0) {
        /* 参数不合法时仍输出，但 CLAMP 已兜底 */
    }

    /* 前后台都填充相同数据 (首次启动前) */
    memcpy(fb_SD, bb_SD, BUFFER_SIZE * sizeof(uint16_t));
    memcpy(fb_SM, bb_SM, BUFFER_SIZE * sizeof(uint16_t));

    /* 启动 DAC+DMA + TIM6 */
    /* STM32F4 TIM6: ARR=0锁死, 改PSC+ARR重配为1MHz */
    htim6.Instance->PSC = 41;
    htim6.Instance->ARR = 1;
    htim6.Instance->EGR = TIM_EGR_UG;
    HAL_DAC_Start_DMA(&hdac, DAC_CHANNEL_1, (uint32_t *)fb_SD, BUFFER_SIZE, DAC_ALIGN_12B_R);
    HAL_DAC_Start_DMA(&hdac, DAC_CHANNEL_2, (uint32_t *)fb_SM, BUFFER_SIZE, DAC_ALIGN_12B_R);
    HAL_TIM_Base_Start(&htim6);

    dma_running = 1;

    return ret;
}

int DualChannel_Update(const DualChannel_Params *params)
{
    if (params == NULL) {
        return SIGNAL_GEN_OK;
    }

    if (!dma_running) {
        return SIGNAL_GEN_CLIPPING;
    }

    /* 保存参数 */
    memcpy(&current_params, params, sizeof(DualChannel_Params));

    /* 向后台缓冲计算新波形 */
    int ret = compute_buffers(&current_params);
    if (ret != 0) {
        /* 越界但仍切换 — CLAMP 已兜底，调用方可选择忽略或回退 */
    }

    /* 交换前后台 + 同步重启 */
    swap_and_restart();

    return ret;
}

const DualChannel_Params* DualChannel_GetParams(void)
{
    return &current_params;
}

void DualChannel_Stop(void)
{
    if (dma_running) {
        HAL_TIM_Base_Stop(&htim6);
        HAL_DAC_Stop_DMA(&hdac, DAC_CHANNEL_1);
        HAL_DAC_Stop_DMA(&hdac, DAC_CHANNEL_2);
        dma_running = 0;
    }
}
