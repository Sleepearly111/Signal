#ifndef UI_BASIC_H
#define UI_BASIC_H

#include <stdint.h>

/* 全局状态: 当前模式 */
typedef enum {
    MODE_IDLE = 0,
    MODE_BASIC3,    /* 基本(3): 1kHz → 已知电路 2Vpp */
    MODE_BASIC4,    /* 基本(4): 用户指定频率+电压 */
    MODE_CALIBRATE, /* 校准模式: 扫频测 Vpp≥3V */
    MODE_LEARN,     /* 发挥(1): 未知电路自主学习建模 */
    MODE_REPLAY,    /* 发挥(2): 复现未知电路输出 */
} AppMode;

extern volatile AppMode g_mode;
extern volatile uint32_t g_set_freq_hz;   /* 用户设定频率 */
extern volatile float    g_set_vpp;        /* 用户设定靶电压 Vpp */

/* UI 初始化: 串口屏 + 按键中断 */
void UI_Init(void);

/* 串口屏命令解析（由 UI_Service 在主循环中调用） */
void UI_ProcessCommand(uint8_t *buf, uint16_t len);

/* 处理串口屏已接收的完整命令（主循环中调用） */
void UI_Service(void);

/* 更新串口屏显示 */
void UI_UpdateDisplay(uint32_t freq_hz, float vpp);

/* Physical keys: KEY0 starts learning; KEY1 continues after manual rewiring;
 * after learning is complete, KEY1 starts replay. */
void UI_KeyCallback(uint8_t key_id);

/* USART1 接收完成回调：由 HAL_UART_RxCpltCallback 调用 */
void UI_UART_RxCompleteCallback(void);

/* USART1 中断逐字节入队（不要在中断中直接更新屏幕） */
void UI_UART_RxCallback(uint8_t byte);

/* 向串口屏发送指令(发挥模块也用) */
void UI_SendToScreen(const char *fmt, ...);

#endif /* UI_BASIC_H */
