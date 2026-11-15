/**
 * @file    app_config.h
 * @brief   2024C 无线传输信号模拟系统 —— 所有可调参数集中定义
 *
 * 参数按"实测标定"和"出厂默认"分为两类：
 *   TODO: 实测标定 —— 拿到硬件后测量真实值填入
 *   出厂默认    —— 无需标定，直接使用
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>

/* ================================================================
 * 系统参数
 * ================================================================ */

#define SAMPLE_RATE_HZ          1000000U    /* DAC 更新率: 1Msps (TIM6 PSC=41,ARR=1) */
#define BUFFER_SIZE             1000U       /* DMA 缓冲样本数 (1ms 周期) */
#define AM_FREQ_HZ              2000U       /* AM 调制频率: 2kHz (固定) */

/* ================================================================
 * DAC 安全范围 — TODO: V0 实验后填入实测值
 *
 * V0 实验步骤:
 *   V0a: DAC 直流码值扫描 0~4095, 万用表测电压, 找到线性区间
 *   V0b: DAC buffer 开/关对比 40kHz 正弦波, 选较优配置
 *
 * 实测前使用保守默认值，预留上下各 128 码值安全边距。
 * ================================================================ */

#define DAC_CODE_MIN_SAFE       0U          /* V0实测: 0V 起线性良好 */
#define DAC_CODE_MAX_SAFE       3840U       /* V0实测: 3840后输出缓冲饱和 */
#define DAC_CENTER              2048U       /* 直流偏置中点 (1.65V) */

/* ================================================================
 * 模拟链路增益 — TODO: 实测标定
 * ================================================================ */

#define THS4001_GAIN            1.0f        /* 直出, 无放大 (跟随或不接THS4001) */
/* TODO: 用示波器量 DAC 输出 Vpp 和 THS4001 输出 Vpp，
 *       实测 THS4001_GAIN_MEASURED = Vout_pp / Vin_pp
 *       写入下面并改用 MEASURED 值 */

#define DAC_FULLSCALE_VPP       3.30f       /* DAC 满幅 Vpp (隔直后, 理论值) */
/* TODO: 实测 */
#define OUTPUT_FULLSCALE_VPP    (DAC_FULLSCALE_VPP * THS4001_GAIN)  /* 6.60Vpp */

/* ================================================================
 * 出厂默认参数 (上电时的初始输出)
 * ================================================================ */

#define DEFAULT_FC_HZ           35000U      /* 载波频率 35kHz */
#define DEFAULT_MODE_CW         1           /* 1=CW, 0=AM */
#define DEFAULT_MA              0.5f        /* AM 调制度 (仅 AM 模式生效) */
#define DEFAULT_VRMS_MV          500U       /* SD 有效值 500mV */
#define DEFAULT_TAU_US          100U        /* SM 时延 100μs */
#define DEFAULT_ATTEN_DB        6.0f        /* SM 衰减 6dB */
#define DEFAULT_PHI_DEG         60.0f       /* SM 初相 60° */

/* ================================================================
 * 参数范围限制 (用于输入校验)
 * ================================================================ */

#define FC_HZ_MIN               30000U
#define FC_HZ_MAX               40000U
#define FC_HZ_STEP              1000U

#define VRMS_MV_MIN             100U
#define VRMS_MV_MAX             1000U
#define VRMS_MV_STEP            100U

#define MA_MIN                  0.3f
#define MA_MAX                  0.9f
#define MA_STEP                 0.1f

#define TAU_US_MIN              50U
#define TAU_US_MAX              200U
#define TAU_US_STEP             30U

#define ATTEN_DB_MIN            0.0f
#define ATTEN_DB_MAX            20.0f
#define ATTEN_DB_STEP           2.0f

#define PHI_DEG_MIN             0.0f
#define PHI_DEG_MAX             180.0f
#define PHI_DEG_STEP            30.0f

/* ================================================================
 * 物理常数
 * ================================================================ */

#define PI                      3.14159265359f
#define RMS_TO_VPP              (2.0f * 1.41421356237f)  /* 2√2 */

#endif /* APP_CONFIG_H */
