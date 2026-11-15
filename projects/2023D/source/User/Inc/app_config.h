#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* ========= 2023D 信号调制方式识别与参数估计 — 全部可调参数 ========= */

/* ===== 时钟(由 CubeMX 时钟树决定，勿改) ===== */
#define TIM8_CLK_HZ        168000000UL  /* APB2 定时器时钟 */
#define TIM3_CLK_HZ        84000000UL   /* APB1 定时器时钟 */
#define TIM7_CLK_HZ        84000000UL

/* ===== 包络路 (ADC1@PB1, TIM3 TRGO 触发) ===== */
#define ENV_SAMPLE_RATE    200000       /* Hz = 84MHz/420, 与 CubeMX 里 TIM3 Period=420-1 一致 */
#define ENV_SAMPLES        8000         /* 40ms 采集窗 */
#define ENV_BIAS_ADC       1914.0f       /* 无信号时ADC p50 (抬升电路DC偏置), 2026-07-23实测 */
#define MA_CAL_K           10.0f         /* ma校准系数: ma=K*(p95-p5)/p50, 用已知ma微调 */

/* ===== 频率路 (TIM8 CH1@PC6 输入捕获, 168MHz 计数) ===== */
#define CAP_IC_PRESCALER   8            /* 每 8 个上升沿捕获一次(CubeMX IC Prescaler=DIV8) */
#define CAP_SAMPLES        10240        /* >40ms@2.025MHz(10125), 防止提前装满 */
#define CAP_EXPECTED_COUNT 10000        /* 40ms@2MHz 理论捕获数 */
#define FREQ_DECIM         5            /* 每 5 个捕获合成 1 个瞬时频率(=40 载波周期) */
#define FREQ_MAX_SAMPLES   (CAP_SAMPLES / FREQ_DECIM)   /* 2048 */
#define FREQ_SAMPLE_RATE   50000.0f     /* 名义瞬时频率序列采样率 Hz (2MHz/40) */

#define CAPTURE_TIMEOUT_MS 300          /* 采集超时: 提示查接线/FREQ_SOURCE */

/* ===== 捕获路间隔验证 (40ms 窗口后质量判定) ===== */
#define CAP_MIN_CARRIER_TICKS    200    /* TIM8 ticks: 低于此值视为无效(定时器回绕/噪声), 约1.19us */
#define CAP_MAX_INTERVAL_TICKS  5000    /* TIM8 ticks: 超过此值视为异常长间隔(丢边沿/2ASK低电平), 约29.8us */
#define CAP_MIN_EDGES_FOR_FREQ   500    /* 40ms 内最少捕获事件数才认频率路有效 */

/* ===== 瞬时频率有效性过滤 ===== */
#define FREQ_VALID_MAX_TICKS   CAP_MAX_INTERVAL_TICKS  /* 单间隔超过此值→该频率点无效 */
#define FREQ_VALID_MIN_COUNT   10     /* 最少有效频率点数, 低于此值频率路整体不可用 */
#define ASK_FREQ_MAX_COUNT     1200   /* OOK实测6kbps约764点，CW约2001点 */

/* ===== 频率路数据来源 (FPGA 分支切换, 设计文档 §9: 换线=换宏+重烧) ===== */
#define FREQ_SRC_TIM8      0
#define FREQ_SRC_FPGA      1
#define FREQ_SOURCE        FREQ_SRC_TIM8

/* ===== 识别判决门限 ===== */
#define ENV_RIPPLE_TH      0.030f  /* 100mVpp+20dB实测: CW约0.0225~0.0241，留出噪声裕量 */
#define ENV_TONE_TH        0.0020f /* 包络1~5kHz归一化谱峰: CW最高0.00021, AM20%为0.00499 */
#define FREQ_DEV_TH_HZ     800.0f  /* TODO: 实测标定 — 瞬时频率偏离中位数视为"频率偏移"的门限 */
#define MID_FRACTION_TH    0.10f   /* TODO: 实测标定 — 中间带占比 >此值为正弦(AM/FM), <为二电平(2ASK/2FSK) */
#define GLITCH_RUN_MAX     2       /* 频偏游程<=此值算孤立毛刺(2PSK), >=SUSTAIN_RUN 算电平驻留 */
#define SUSTAIN_RUN        4       /* 频偏游程>=此值算持续偏移(FM/2FSK) */
#define GLITCH_MIN_COUNT   5       /* 至少几个孤立毛刺才判 2PSK */

/* ===== 参数候选(题目给定, 估计值向候选吸附) ===== */
#define F_CANDIDATES_HZ    {1000.0f, 2000.0f, 3000.0f, 4000.0f, 5000.0f}
#define F_CANDIDATE_NUM    5
#define RC_CANDIDATES_BPS  {6000, 8000, 10000}
#define RC_CANDIDATE_NUM   3

/* ===== uo 解调输出 (DAC1@PA4, TIM7 触发 + DMA 循环) ===== */
#define DAC_BUF_LEN        2048
#define DAC_TEST_SINE_HZ   1000.0f  /* V2 验证用内置正弦频率 */

/* ===== 串口屏 (陶晶驰, USART1@115200; 调试打印 USART2@115200) ===== */
#define HMI_OBJ_TYPE       "t0"     /* 调制类型 */
#define HMI_OBJ_P1         "t1"     /* 参数1: ma / mf / Rc */
#define HMI_OBJ_P2         "t2"     /* 参数2: F / dfmax / h */
#define HMI_OBJ_STATUS     "t3"     /* 状态提示 */

#endif /* APP_CONFIG_H */
