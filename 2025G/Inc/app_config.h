#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* ===== AD603 VGA 控制参数 ===== */
/* TODO: 实测标定 — 板子到了后修改以下值 */

/* AD603 增益曲线: Gain(dB) = Vg × SLOPE + OFFSET
 * 实测拟合(5点): G = 75.97×Vg - 49.30 (dB)
 * 有效范围: Vg=0.7~1.1V 对应 3~33dB
 */
#define AD603_VG_MIN      0.0f
#define AD603_VG_MAX      1.446f
#define AD603_GAIN_SLOPE  75.97f    /* 实测拟合 */
#define AD603_GAIN_OFFSET -49.30f   /* 实测拟合 */

/* 内部DAC: STM32F407 DAC1 CH1 → AD603 VG引脚
 * DAC 满幅输出参考电压 (V)
 */
#define AD603_DAC_VREF    3.3f    /* 实测标定: PA4满幅=3.3V */

/* ===== 信号链幅度参数 ===== */
/* AD9833 默认输出幅度 Vpp (典型值 0.6Vpp) */
#define AD9833_DEFAULT_VPP 0.66f   /* 实测标定: B口 660mVpp */

/* AD8052 固定放大倍数 */
#define AD8052_GAIN       5.0f    /* TODO: 实测标定 */

/* 题目要求: 各频点输出电压 Vpp 最大值不小于 3V */
#define OUTPUT_VPP_MIN    3.0f

/* 基本(2) 目标: 各频点稳定输出 3.5Vpp (要求 >=3V, 取 3.5V 留余量) */
#define OUTPUT_VPP_TARGET 3.5f

/* 信号链总增益上限: AD603最大42dB + AD8052 ×5 */
#define OUTPUT_VPP_MAX    4.0f

/* ===== 测量参数 ===== */
/* ADS8688 幅度测量采样点数 */
#define MEASURE_SAMPLES   256     /* TODO: 实测后可调整 */

/* 采样平均次数（消除噪声） */
#define MEASURE_AVG_COUNT 2

/* ===== 题目已知模型电路 H(s) =====
 * H(s) = 5 / (1e-8*s² + 3e-4*s + 1)   题目给定的已知电路模型
 */
#define HS_A2  1e-8f     /* s² 系数 (题目值 10^-8) */
#define HS_A1  0.0003f   /* s 系数 */
#define HS_A0  1.0f      /* 常数项 */
#define HS_B0  5.0f      /* 分子 */

/* ===== 频率限制 ===== */
#define FREQ_MIN_HZ      100
#define FREQ_MAX_HZ      1100000  /* 1.1MHz，留余量 (基本2用) */
#define FREQ_BASIC4_MAX  3000     /* 基本(4) 频率上限 100~3kHz (题目要求) */
#define FREQ_STEP_HZ     100

/* ===== 电压设定 ===== */
#define VOLT_MIN_VPP     1.0f     /* Vpp */
#define VOLT_MAX_VPP     2.0f     /* Vpp */
#define VOLT_STEP_VPP    0.1f

/* ===== 发挥部分 DSP 参数 ===== */
/* FFT 点数(2的幂): 频率分辨率=采样率/点数 ≈250k/2048≈122Hz,加插值可分辨200Hz步进 */
#define DSP_FFT_SIZE      2048

/* ADC 采样率 Hz(ADS8688): >100kHz(Nyquist),取250kHz 兼顾频谱分辨率 */
#define DSP_SAMPLE_RATE   250000

/* DAC 整周期波形点数: 点数多→波形平滑,但高频受 DAC 更新率限制(F407 DAC ~300ksps max) */
#define DSP_DAC_LENGTH    256    /* TODO: 联调可调,高频平滑不够可降到128或64 */

/* FIR/LMS 参数(与参考方案一致,BLOCK_SIZE降至2048省RAM) */
#define LMS_NUM_TAPS      32     /* FIR 阶数 */
#define LMS_STEP_SIZE     0.001f /* LMS 收敛步长 */
#define LMS_BLOCK_SIZE    2048   /* 每次 LMS 处理的采样点数(32阶/2048点够收敛) */

/* 谐波合成参数 */
#define DSP_HARMONIC_MAX  10     /* 最多取到 19 次谐波(奇次) */

#endif /* APP_CONFIG_H */
