/* TJC 的 USART1 异步传输层：只负责字节收发和 ISR 到前台的交接。 */
#ifndef PRESENTATION_INC_TJC_TRANSPORT_H_
#define PRESENTATION_INC_TJC_TRANSPORT_H_

#include <stdbool.h>
#include <stdint.h>

#include "stm32f4xx_hal.h"
#include "tjc_display_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*TjcTransportRxByteCallback)(void *context, uint8_t byte);
typedef void (*TjcTransportSignalCallback)(void *context);

/**
 * @brief USART1 传输状态及 ISR/前台共享接收环。
 * @note 本结构只知道 UART 字节，不解析 page ID、按键、addt 或测量数据。
 */
typedef struct
{
    UART_HandleTypeDef *uart;           /* TJC 独占的 CubeMX UART 句柄。 */
    volatile uint8_t rx_byte;           /* HAL 单字节中断接收暂存。 */
    volatile uint8_t rx_ring[TJC_UART_RX_RING_SIZE]; /* ISR 写入、前台取出的字节环。 */
    volatile uint8_t rx_head;           /* 环形缓冲写指针，仅 ISR 推进。 */
    volatile uint8_t rx_tail;           /* 环形缓冲读指针，仅前台推进。 */
    volatile uint32_t rx_overflow_count;/* 环满时丢弃的字节数，供诊断。 */
    volatile uint8_t rx_rearm_pending;  /* 中断重挂接失败后由前台重试。 */
    volatile uint8_t tx_complete_pending; /* UART TX 完成 ISR 置位，前台再通知页面状态机。 */
    volatile uint8_t error_pending;       /* UART 错误 ISR 置位，前台再中止页面事务。 */
    TjcTransportRxByteCallback on_rx_byte; /* 前台消费到一个字节时调用的页面协议回调。 */
    TjcTransportSignalCallback on_tx_complete; /* 一笔异步 UART 发送完成时的前台信号回调。 */
    TjcTransportSignalCallback on_error; /* UART 错误时的前台信号回调。 */
    void *callback_context;             /* 三类回调共享的页面控制器上下文。 */
} TjcTransport;

/** @brief 绑定 USART1 和页面协议回调，并启动单字节接收中断。 */
void TjcTransport_Init(TjcTransport *transport, UART_HandleTypeDef *uart,
                       TjcTransportRxByteCallback on_rx_byte,
                       TjcTransportSignalCallback on_tx_complete,
                       TjcTransportSignalCallback on_error,
                       void *callback_context);

/** @brief 主循环调用：重挂接接收中断并把 RX 环中的字节交给页面协议层。 */
void TjcTransport_Service(TjcTransport *transport);

/** @brief 启动一笔异步字节发送；调用方必须保证 bytes 在 TX 完成前不被改写。 */
bool TjcTransport_Send(TjcTransport *transport, const uint8_t *bytes, uint16_t length);

/* 以下函数只由唯一 HAL UART 回调快速转发；不解析协议、不格式化显示。 */
void TjcTransport_OnUartRxComplete(TjcTransport *transport, UART_HandleTypeDef *uart);
void TjcTransport_OnUartTxComplete(TjcTransport *transport, UART_HandleTypeDef *uart);
void TjcTransport_OnUartError(TjcTransport *transport, UART_HandleTypeDef *uart);

#ifdef __cplusplus
}
#endif

#endif /* PRESENTATION_INC_TJC_TRANSPORT_H_ */
