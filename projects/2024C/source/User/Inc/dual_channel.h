/**
 * @file    dual_channel.h
 * @brief   2024C 双通道 DDS 引擎 —— 头文件
 *
 * 负责 SD (直达信号) 和 SM (多径信号) 的数字域生成。
 * 参数更新时 CPU 计算 DMA 缓冲区，DMA 循环搬运到 DAC。
 * 每通道 A/B 双缓冲，统一停机同步重启。
 */

#ifndef DUAL_CHANNEL_H
#define DUAL_CHANNEL_H

#include <stdint.h>

/* 信号生成错误码 */
#define SIGNAL_GEN_OK            0
#define SIGNAL_GEN_CLIPPING     -1   /* DAC 码值超出安全范围 */

/* ================================================================
 * 参数结构体 —— 所有可调参数打包
 * ================================================================ */

typedef struct {
    uint32_t fc_hz;         /* 载波频率 30k~40k Hz */
    uint8_t  is_cw;         /* 1=CW, 0=AM */
    float    ma;             /* AM 调制度 0.0(CW) 或 0.3~0.9 */
    uint32_t vrms_mv;       /* SD 有效值 100~1000 mV */
    uint32_t tau_us;        /* SM 时延 50~200 μs, 步进 30μs */
    float    atten_db;       /* SM 衰减 0~20 dB, 步进 2dB */
    float    phi_deg;        /* SM 初相 0~180°, 步进 30° */
} DualChannel_Params;

/* ================================================================
 * 公共接口
 * ================================================================ */

/**
 * @brief 初始化双通道 DDS 引擎
 *        分配 A/B 双缓冲，设置默认参数，启动 DAC+DMA 输出
 * @param params  初始参数（可传 NULL 使用出厂默认值）
 * @retval 0=成功
 */
int DualChannel_Init(const DualChannel_Params *params);

/**
 * @brief 更新输出参数
 *        在后台缓冲计算新波形 → 停机 → 交换缓冲 → 同步重启
 * @param params  新参数（NULL = 不更新）
 * @retval SIGNAL_GEN_OK(0)        成功
 * @retval SIGNAL_GEN_CLIPPING(-1) DAC 码值越界，参数被拒绝
 */
int DualChannel_Update(const DualChannel_Params *params);

/**
 * @brief 获取当前生效的参数
 */
const DualChannel_Params* DualChannel_GetParams(void);

/**
 * @brief 停止 DAC 输出
 */
void DualChannel_Stop(void);

#endif /* DUAL_CHANNEL_H */
