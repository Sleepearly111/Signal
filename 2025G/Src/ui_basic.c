#include "ui_basic.h"
#include "app_config.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/* ===== 全局状态 ===== */
volatile AppMode  g_mode         = MODE_IDLE;
volatile uint32_t g_set_freq_hz  = 1000;
volatile float    g_set_vpp      = 2.0f;

/* 串口屏接收缓冲 (复用 USART1 RXNE 中断) */
#define UI_RX_BUF_SIZE 32
static uint8_t  rx_buf[UI_RX_BUF_SIZE];
static uint16_t rx_sta;

/* USART 句柄 */
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;

/* 当前 printf 输出串口 (1=串口屏, 2=调试) */
static uint8_t print_uart = 2;

/* ===== printf 重定向 ===== */
int _write(int file, char *ptr, int len)
{
    if (print_uart == 1) {
        HAL_UART_Transmit(&huart1, (uint8_t*)ptr, len, HAL_MAX_DELAY);
    } else {
        HAL_UART_Transmit(&huart2, (uint8_t*)ptr, len, HAL_MAX_DELAY);
    }
    return len;
}

/* 向串口屏发送指令 */
void UI_SendToScreen(const char *fmt, ...)
{
    char buf[100];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    print_uart = 1;
    printf("%s\xff\xff\xff", buf);
    print_uart = 2;
}

void UI_Init(void)
{
    rx_sta = 0;
    memset(rx_buf, 0, UI_RX_BUF_SIZE);

    /* 使能 USART1 RXNE 中断 */
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_RXNE);

    /* 启动画面 */
    UI_SendToScreen("page 0");
    UI_SendToScreen("message.txt=\"2025G 电路模型探究\"");
}

/*
 * 串口屏命令解析
 * 帧格式: 命令字节 + 参数 + 0xFF 0xFF 0xFF 帧尾
 *
 * 0x01 0x00 → 基本(3): 1kHz 固定 2Vpp
 * 0x02 xx yy → 基本(4): xx=频率高字节 yy=频率低字节 (freq = (xx<<8|yy)*100Hz)
 * 0x03 xx → 设置目标电压 Vpp (xx = Vpp×10, 如 20=2.0V)
 */
void UI_ProcessCommand(uint8_t *buf, uint16_t len)
{
    if (len < 2) return;

    switch (buf[0]) {
    case 0x01:  /* 基本(3) */
        g_mode = MODE_BASIC3;
        UI_SendToScreen("message.txt=\"基本(3): 1kHz→2Vpp\"");
        break;

    case 0x06:  /* 发挥(2): 启动复现 → 推理生成未知电路输出 */
        g_mode = MODE_REPLAY;
        UI_SendToScreen("message.txt=\"复现输出中...\"");
        break;

    case 0x05:  /* 发挥(1): 启动学习键 → 自主学习未知电路 */
        g_mode = MODE_LEARN;
        UI_SendToScreen("message.txt=\"学习建模中...\"");
        break;

    case 0x04:  /* 基本(2): 设频率 → 输出稳定 3.5Vpp */
        if (len >= 3) {
            uint32_t f = ((uint32_t)buf[1] << 8) | buf[2];
            f *= 100;  /* 步长 100Hz */
            if (f < FREQ_MIN_HZ) f = FREQ_MIN_HZ;
            if (f > FREQ_MAX_HZ) f = FREQ_MAX_HZ;
            g_set_freq_hz = f;
            g_mode = MODE_CALIBRATE;
        }
        break;

    case 0x02:  /* 基本(4): 设置频率 (100~3kHz) */
        if (len >= 3) {
            uint32_t f = ((uint32_t)buf[1] << 8) | buf[2];
            f *= 100;  /* 步长 100Hz */
            if (f < FREQ_MIN_HZ) f = FREQ_MIN_HZ;
            if (f > FREQ_BASIC4_MAX) f = FREQ_BASIC4_MAX;  /* 基本4 上限 3kHz */
            g_set_freq_hz = f;
            g_mode = MODE_BASIC4;
        }
        break;

    case 0x03:  /* 设置目标电压 Vpp */
        if (len >= 2) {
            float v = (float)buf[1] / 10.0f;
            if (v < VOLT_MIN_VPP) v = VOLT_MIN_VPP;
            if (v > VOLT_MAX_VPP) v = VOLT_MAX_VPP;
            g_set_vpp = v;
            g_mode = MODE_BASIC4;
        }
        break;

    default:
        break;
    }
}

void UI_UpdateDisplay(uint32_t freq_hz, float vpp)
{
    UI_SendToScreen("t0.txt=\"%.2fkHz\"", (float)freq_hz / 1000.0f);
    UI_SendToScreen("t1.txt=\"%.3fVpp\"", vpp);

    char *mode_str = "IDLE";
    if (g_mode == MODE_BASIC3) mode_str = "基本(3)";
    else if (g_mode == MODE_BASIC4) mode_str = "基本(4)";
    else if (g_mode == MODE_CALIBRATE) mode_str = "基本(2)";
    else if (g_mode == MODE_LEARN)  mode_str = "学习建模";
    else if (g_mode == MODE_REPLAY) mode_str = "复现输出";
    UI_SendToScreen("message.txt=\"%s\"", mode_str);
}

/*
 * 按键 EXTI 回调
 * KEY0 (PE4) → 基本(3)
 * KEY1 (PE3) → 基本(4)
 */
void UI_KeyCallback(uint8_t key_id)
{
    if (key_id == 0) {  /* KEY0 */
        g_mode = MODE_BASIC3;
    } else if (key_id == 1) {  /* KEY1 */
        g_mode = MODE_BASIC4;
    }
}

/* ===== USART1 RXNE 中断回调 (逐字节接收串口屏数据) ===== */
void UI_UART_RxCallback(uint8_t byte)
{
    /* 帧尾检测: 连续 3 个 0xFF */
    if (byte == 0xFF) {
        rx_sta++;
        if (rx_sta >= 3) {
            /* 帧接收完成，解析 */
            uint16_t data_len = (rx_sta > 3) ? (rx_sta - 3) : 0;
            if (data_len > 0 && data_len <= UI_RX_BUF_SIZE) {
                UI_ProcessCommand(rx_buf, data_len);
            }
            rx_sta = 0;
            memset(rx_buf, 0, UI_RX_BUF_SIZE);
        }
        return;
    }

    /* 非 0xFF 字节: 如果之前收到了 0xFF，先存入缓冲 */
    if (rx_sta > 0) {
        rx_buf[rx_sta - 1] = byte;
        rx_sta++;
        if (rx_sta - 1 >= UI_RX_BUF_SIZE) {
            rx_sta = 0;  /* 溢出，丢弃 */
        }
    }
}
