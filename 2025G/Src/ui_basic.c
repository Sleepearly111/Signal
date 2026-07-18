#include "ui_basic.h"
#include "app_config.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/* ===== 全局状态 ===== */
volatile AppMode  g_mode         = MODE_IDLE;
volatile uint32_t g_set_freq_hz  = 2000;
volatile float    g_set_vpp      = 1.8f;

/* 串口屏接收缓冲。
 * 新屏幕协议使用 ASCII：SET,<频率Hz>,<峰峰值mV>\n
 * 例：SET,1000,1500\n → 基本(4)，1kHz、1.5Vpp。
 * 同时保留旧版二进制帧：命令 + 参数 + FF FF FF。 */
#define UI_RX_BUF_SIZE 48
static uint8_t  rx_buf[UI_RX_BUF_SIZE];
static uint16_t rx_len;
static uint8_t  rx_ff_count;

/* 中断仅接收和入队；命令在主循环执行，避免中断里阻塞发送串口。 */
static uint8_t          cmd_buf[UI_RX_BUF_SIZE];
static uint16_t         cmd_len;
static volatile uint8_t cmd_ready;
static uint8_t          uart1_rx_byte;

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
    rx_len = 0;
    rx_ff_count = 0;
    cmd_len = 0;
    cmd_ready = 0;
    memset(rx_buf, 0, UI_RX_BUF_SIZE);

    /* 用 HAL 的中断接收接口注册接收字节；仅打开 RXNE 不会产生完成回调。 */
    HAL_UART_Receive_IT(&huart1, &uart1_rx_byte, 1);

    /* 启动画面 */
    UI_SendToScreen("bkcmd=0");  /* 屏幕不回传每条指令的成功应答，避免干扰控制命令 */
    UI_SendToScreen("page 0");
    UI_SendToScreen("message.txt=\"2025G 电路模型探究\"");
}

/*
 * 串口屏命令解析。
 * 推荐屏幕使用 ASCII 帧：
 *   SET,<频率Hz>,<峰峰值mV>\n  例如 SET,1000,1500\n
 *   B3\n / B2,<频率Hz>\n / LEARN\n / REPLAY\n / STOP\n
 * 精简控件方案可直接发送 Number.val 的四字节值（小端）：
 *   A2 + <freq 4B> + FF FF FF              → 基本(2)
 *   A3 + FF FF FF                          → 基本(3)
 *   A4 + <freq 4B> + <vpp_mV 4B> + FF...  → 基本(4)
 * 屏幕事件用 print nX.val 即可，不需要隐藏文本/数据变量。
 *
 * 仍兼容旧版二进制帧：命令字节 + 参数 + 0xFF 0xFF 0xFF 帧尾
 *
 * 0x01 0x00 → 基本(3): 1kHz 固定 2Vpp
 * 0x02 xx yy → 基本(4): xx=频率高字节 yy=频率低字节 (freq = (xx<<8|yy)*100Hz)
 * 0x03 xx → 设置目标电压 Vpp (xx = Vpp×10, 如 20=2.0V)
 */
static uint32_t UI_ReadU32LE(const uint8_t *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

void UI_ProcessCommand(uint8_t *buf, uint16_t len)
{
    if (len == 0) return;

    /* 精简屏幕方案：直接接收 Number.val 的四字节小端整数。 */
    if (buf[0] == 0xA2 && len == 5) {
        uint32_t freq_hz = UI_ReadU32LE(&buf[1]);
        if (freq_hz < FREQ_MIN_HZ) freq_hz = FREQ_MIN_HZ;
        if (freq_hz > FREQ_MAX_HZ) freq_hz = FREQ_MAX_HZ;
        g_set_freq_hz = (freq_hz / FREQ_STEP_HZ) * FREQ_STEP_HZ;
        g_mode = MODE_CALIBRATE;
        UI_SendToScreen("message.txt=\"基本(2): %luHz\"", g_set_freq_hz);
        return;
    }

    if (buf[0] == 0xA3 && len == 1) {
        g_mode = MODE_BASIC3;
        UI_SendToScreen("message.txt=\"基本(3): 1kHz→2Vpp\"");
        return;
    }

    if (buf[0] == 0xA4 && len == 9) {
        uint32_t freq_hz = UI_ReadU32LE(&buf[1]);
        uint32_t vpp_mv  = UI_ReadU32LE(&buf[5]);
        if (freq_hz < FREQ_MIN_HZ) freq_hz = FREQ_MIN_HZ;
        if (freq_hz > FREQ_BASIC4_MAX) freq_hz = FREQ_BASIC4_MAX;
        if (vpp_mv < 1000UL) vpp_mv = 1000UL;
        if (vpp_mv > 2000UL) vpp_mv = 2000UL;

        g_set_freq_hz = (freq_hz / FREQ_STEP_HZ) * FREQ_STEP_HZ;
        g_set_vpp = (float)((vpp_mv / 100UL) * 100UL) / 1000.0f;
        g_mode = MODE_BASIC4;
        UI_SendToScreen("message.txt=\"基本(4): %luHz / %.1fVpp\"",
                        g_set_freq_hz, g_set_vpp);
        return;
    }

    /* 推荐的可读 ASCII 协议。峰峰值以 mV 传递，避免屏端浮点格式问题。 */
    if (buf[0] >= 'A' && buf[0] <= 'Z') {
        char line[UI_RX_BUF_SIZE];
        uint16_t line_len = (len < UI_RX_BUF_SIZE - 1) ? len : UI_RX_BUF_SIZE - 1;
        memcpy(line, buf, line_len);
        line[line_len] = '\0';

        unsigned long freq_hz;
        unsigned long vpp_mv;
        if (sscanf(line, "SET,%lu,%lu", &freq_hz, &vpp_mv) == 2) {
            if (freq_hz < FREQ_MIN_HZ) freq_hz = FREQ_MIN_HZ;
            if (freq_hz > FREQ_BASIC4_MAX) freq_hz = FREQ_BASIC4_MAX;
            if (vpp_mv < 1000UL) vpp_mv = 1000UL;
            if (vpp_mv > 2000UL) vpp_mv = 2000UL;

            /* 保持题目基本(4)的 100Hz / 0.1Vpp 设定步长。 */
            freq_hz = (freq_hz / FREQ_STEP_HZ) * FREQ_STEP_HZ;
            vpp_mv = (vpp_mv / 100UL) * 100UL;

            g_set_freq_hz = (uint32_t)freq_hz;
            g_set_vpp = (float)vpp_mv / 1000.0f;
            g_mode = MODE_BASIC4;
            UI_SendToScreen("message.txt=\"设定 %luHz / %.1fVpp\"",
                            freq_hz, g_set_vpp);
        } else if (sscanf(line, "B2,%lu", &freq_hz) == 1) {
            if (freq_hz < FREQ_MIN_HZ) freq_hz = FREQ_MIN_HZ;
            if (freq_hz > FREQ_MAX_HZ) freq_hz = FREQ_MAX_HZ;
            freq_hz = (freq_hz / FREQ_STEP_HZ) * FREQ_STEP_HZ;
            g_set_freq_hz = (uint32_t)freq_hz;
            g_mode = MODE_CALIBRATE;
        } else if (strcmp(line, "B3") == 0) {
            g_mode = MODE_BASIC3;
            UI_SendToScreen("message.txt=\"基本(3): 1kHz→2Vpp\"");
        } else if (strcmp(line, "LEARN") == 0) {
            g_mode = MODE_LEARN;
            UI_SendToScreen("message.txt=\"学习建模中...\"");
        } else if (strcmp(line, "REPLAY") == 0) {
            g_mode = MODE_REPLAY;
            UI_SendToScreen("message.txt=\"复现输出中...\"");
        } else if (strcmp(line, "STOP") == 0) {
            g_mode = MODE_IDLE;
            UI_SendToScreen("message.txt=\"已停止\"");
        }
        return;
    }

    switch (buf[0]) {
    case 0x01:  /* 基本(3) */
        if (len < 2) break;  /* 0x01 + FF FF FF 可能是屏幕的“成功应答” */
        g_mode = MODE_BASIC3;
        UI_SendToScreen("message.txt=\"基本(3): 1kHz→2Vpp\"");
        break;

    case 0x06:  /* 发挥(2): 启动复现 → 推理生成未知电路输出 */
        if (len < 2) break;
        g_mode = MODE_REPLAY;
        UI_SendToScreen("message.txt=\"复现输出中...\"");
        break;

    case 0x05:  /* 发挥(1): 启动学习键 → 自主学习未知电路 */
        if (len < 2) break;
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

void UI_Service(void)
{
    uint8_t local_buf[UI_RX_BUF_SIZE];
    uint16_t local_len = 0;
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    if (cmd_ready) {
        local_len = cmd_len;
        memcpy(local_buf, cmd_buf, local_len);
        cmd_ready = 0;
    }
    if (!primask) {
        __enable_irq();
    }

    if (local_len > 0) {
        UI_ProcessCommand(local_buf, local_len);
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
 * 发挥(1) 两点法学习状态机
 * KEY0: 校准(AD637→装置输出), 先1kHz再60kHz
 * KEY1: 测量(AD637→电路输出), 先1kHz再60kHz, 收齐后判类型
 */
#define LEARN_FREQ_COUNT 2
const uint32_t learn_freqs[LEARN_FREQ_COUNT] = {3000, 1000000};

volatile uint8_t  learn_phase     = 0;  /* 0=校准, 1=测量, 2=完成 */
volatile uint8_t  learn_freq_idx  = 0;
volatile uint8_t  learn_need_cal  = 0;
volatile uint8_t  learn_need_meas = 0;
volatile uint32_t learn_cur_freq  = 0;
volatile float    learn_dev_vpp[LEARN_FREQ_COUNT];
volatile float    learn_cir_vpp[LEARN_FREQ_COUNT];
volatile float    learn_dev_gain[LEARN_FREQ_COUNT];

void UI_KeyCallback(uint8_t key_id)
{
    if (key_id == 0) {  /* KEY0: 校准(637→装置输出) */
        if (learn_phase == 0) {
            learn_cur_freq = learn_freqs[learn_freq_idx];
            learn_need_cal = 1;
            printf("KEY0: 校准%luHz\r\n", learn_cur_freq);
        } else {
            printf("已校准, 请按KEY1\r\n");
        }
    } else if (key_id == 1) {  /* KEY1: 测量(637→电路输出) */
        if (learn_phase == 0) {
            printf("请先按KEY0校准\r\n");
        } else if (learn_phase == 1) {
            learn_cur_freq = learn_freqs[learn_freq_idx];
            learn_need_meas = 1;
            printf("KEY1: 测量%luHz\r\n", learn_cur_freq);
        } else {
            float r0 = (learn_cir_vpp[0] > 5) ? (learn_dev_vpp[0] * 1000 / learn_cir_vpp[0]) : 999;
            float r1 = (learn_cir_vpp[1] > 5) ? (learn_dev_vpp[1] * 1000 / learn_cir_vpp[1]) : 999;
            int lo = (r0 < 2), hi = (r1 < 2);
            const char *type = "?";
            if (lo && !hi) type = "低通";
            else if (!lo && hi) type = "高通";
            else if (!lo && !hi) type = "带通";
            else type = "带阻";
            printf(">>> %s <<<\r\n", type);
        }
    }
}

static void UI_ResetRxBuffer(void)
{
    rx_len = 0;
    rx_ff_count = 0;
}

static void UI_QueueReceivedCommand(void)
{
    if (rx_len > 0 && !cmd_ready) {
        memcpy(cmd_buf, rx_buf, rx_len);
        cmd_len = rx_len;
        cmd_ready = 1;
    }
    UI_ResetRxBuffer();
}

void UI_UART_RxCompleteCallback(void)
{
    UI_UART_RxCallback(uart1_rx_byte);
    HAL_UART_Receive_IT(&huart1, &uart1_rx_byte, 1);
}

/* ===== USART1 中断回调：接收一字节并在完整帧时入队 ===== */
void UI_UART_RxCallback(uint8_t byte)
{
    /* ASCII 命令以换行结束。 */
    if (byte == '\n') {
        if (rx_ff_count == 0) {
            UI_QueueReceivedCommand();
        } else {
            UI_ResetRxBuffer();
        }
        return;
    }

    /* 保留对旧版二进制帧尾 FF FF FF 的兼容。 */
    if (byte == 0xFF) {
        rx_ff_count++;
        if (rx_ff_count >= 3) {
            UI_QueueReceivedCommand();
        }
        return;
    }

    /* 0xFF 若不构成帧尾，则按普通数据保留。 */
    while (rx_ff_count > 0) {
        if (rx_len >= UI_RX_BUF_SIZE - 1) {
            UI_ResetRxBuffer();
            return;
        }
        rx_buf[rx_len++] = 0xFF;
        rx_ff_count--;
    }

    if (rx_len >= UI_RX_BUF_SIZE - 1) {
        UI_ResetRxBuffer();
        return;
    }
    rx_buf[rx_len++] = byte;
}
