#ifndef CAPTURE_H
#define CAPTURE_H

#include <stdint.h>
#include <stdbool.h>

/* ================================================================
 * 采集执行状态 —— 窗口是否正常结束
 * ================================================================ */
typedef enum {
    CAPTURE_WINDOW_OK = 0,      /* 40ms 窗口正常结束 */
    CAPTURE_TIMEOUT,            /* 300ms 超时 */
    CAPTURE_HAL_ERROR           /* HAL 启动失败 */
} CaptureExecStatus_t;

/* ================================================================
 * 捕获数据质量 —— 频率路可用性评估
 * ================================================================ */
typedef enum {
    CAP_QUALITY_EMPTY      = 0, /* cap_count == 0, 无频率数据           */
    CAP_QUALITY_PARTIAL,        /* cap_count < MIN_EDGES, 频率数据不足  */
    CAP_QUALITY_CONTINUOUS,     /* 边沿持续, 频率路可用                 */
    CAP_QUALITY_TRUNCATED,      /* 缓冲提前装满, 数据被截断             */
    CAP_QUALITY_INVALID         /* HAL_ERROR 或数据损坏                 */
} CaptureQuality_t;

/* ================================================================
 * 捕获路间隔统计 (40ms 窗口内已收事件)
 * ================================================================ */
typedef struct {
    int  cap_count;             /* 40ms 内实际捕获事件数                */
    int  expected_count;        /* 理论期望捕获数 (2MHz→10000)          */
    float capture_ratio;        /* cap_count / expected_count           */
    int  zero_intervals;        /* 相邻时间戳完全相同 (异常)            */
    int  short_intervals;       /* 间隔 < CAP_MIN_CARRIER_TICKS         */
    int  long_intervals;        /* 间隔 > CAP_MAX_INTERVAL_TICKS        */
    int  normal_intervals;      /* 正常间隔数                          */
    uint16_t min_ticks;         /* 最小间隔 TIM8 ticks (排除零)         */
    uint16_t max_ticks;         /* 最大间隔 TIM8 ticks                  */
    bool cap_buffer_full;       /* TIM8 缓冲提前装满 (截断)             */
} CaptureCapStats_t;

/* ---- 初始化 ---------------------------------------------------- */
void Capture_Init(void);

/* ---- 开始一次双通道采集窗口 (40ms) ---------------------------- */
void Capture_Start(void);

/* ---- 主循环轮询 (窗口结束处理 + 超时检测) --------------------- */
void Capture_Service(void);

/* ---- 窗口是否已结束 (数据可用) -------------------------------- */
bool Capture_IsDone(void);
bool Capture_TimedOut(void);

/* ---- 采集执行状态 --------------------------------------------- */
CaptureExecStatus_t Capture_GetExecStatus(void);

/* ---- 频率数据质量 --------------------------------------------- */
CaptureQuality_t Capture_GetQuality(void);

/* ---- 捕获路间隔统计 (仅在窗口结束后有效) --------------------- */
const CaptureCapStats_t *Capture_GetCapStats(void);

/* ---- 数据访问 ------------------------------------------------- */
const uint16_t *Capture_GetEnvelope(int *n);
const uint16_t *Capture_GetCaptures(int *n_cap);

#endif
