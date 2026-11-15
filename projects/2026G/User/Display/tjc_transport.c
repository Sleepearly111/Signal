#include "tjc_transport.h"

#include <string.h>

#if ((TJC_UART_RX_RING_SIZE & (TJC_UART_RX_RING_SIZE - 1U)) != 0U)
#error "TJC_UART_RX_RING_SIZE must be a power of two"
#endif

static void TjcTransport_PushRxByte(TjcTransport *transport, uint8_t byte)
{
    const uint8_t next =
        (uint8_t)((transport->rx_head + 1U) & (TJC_UART_RX_RING_SIZE - 1U));

    if (next == transport->rx_tail)
    {
        transport->rx_overflow_count++;
        return;
    }

    transport->rx_ring[transport->rx_head] = byte;
    transport->rx_head = next;
}

static bool TjcTransport_PopRxByte(TjcTransport *transport, uint8_t *byte)
{
    if ((byte == NULL) || (transport->rx_tail == transport->rx_head))
    {
        return false;
    }

    *byte = transport->rx_ring[transport->rx_tail];
    transport->rx_tail = (uint8_t)((transport->rx_tail + 1U) & (TJC_UART_RX_RING_SIZE - 1U));
    return true;
}

void TjcTransport_Init(TjcTransport *transport, UART_HandleTypeDef *uart,
                       TjcTransportRxByteCallback on_rx_byte,
                       TjcTransportSignalCallback on_tx_complete,
                       TjcTransportSignalCallback on_error,
                       void *callback_context)
{
    if ((transport == NULL) || (uart == NULL))
    {
        return;
    }

    (void)memset(transport, 0, sizeof(*transport));
    transport->uart = uart;
    transport->on_rx_byte = on_rx_byte;
    transport->on_tx_complete = on_tx_complete;
    transport->on_error = on_error;
    transport->callback_context = callback_context;

    if (HAL_UART_Receive_IT(transport->uart, (uint8_t *)&transport->rx_byte, 1U) != HAL_OK)
    {
        transport->rx_rearm_pending = 1U;
    }
}

void TjcTransport_Service(TjcTransport *transport)
{
    uint8_t byte;

    if ((transport == NULL) || (transport->uart == NULL))
    {
        return;
    }

    if (transport->rx_rearm_pending != 0U)
    {
        if (HAL_UART_Receive_IT(transport->uart, (uint8_t *)&transport->rx_byte, 1U) == HAL_OK)
        {
            transport->rx_rearm_pending = 0U;
        }
    }

    while (TjcTransport_PopRxByte(transport, &byte))
    {
        if (transport->on_rx_byte != NULL)
        {
            transport->on_rx_byte(transport->callback_context, byte);
        }
    }

    if (transport->tx_complete_pending != 0U)
    {
        transport->tx_complete_pending = 0U;
        if (transport->on_tx_complete != NULL)
        {
            transport->on_tx_complete(transport->callback_context);
        }
    }
    if (transport->error_pending != 0U)
    {
        transport->error_pending = 0U;
        if (transport->on_error != NULL)
        {
            transport->on_error(transport->callback_context);
        }
    }
}

bool TjcTransport_Send(TjcTransport *transport, const uint8_t *bytes, uint16_t length)
{
    return (transport != NULL) && (transport->uart != NULL) && (bytes != NULL) && (length != 0U) &&
           (HAL_UART_Transmit_IT(transport->uart, (uint8_t *)bytes, length) == HAL_OK);
}

void TjcTransport_OnUartRxComplete(TjcTransport *transport, UART_HandleTypeDef *uart)
{
    if ((transport == NULL) || (uart != transport->uart))
    {
        return;
    }

    TjcTransport_PushRxByte(transport, transport->rx_byte);
    if (HAL_UART_Receive_IT(transport->uart, (uint8_t *)&transport->rx_byte, 1U) != HAL_OK)
    {
        transport->rx_rearm_pending = 1U;
    }
}

void TjcTransport_OnUartTxComplete(TjcTransport *transport, UART_HandleTypeDef *uart)
{
    if ((transport != NULL) && (uart == transport->uart))
    {
        /* ISR 只通知；页面状态机在 TjcTransport_Service 前台推进。 */
        transport->tx_complete_pending = 1U;
    }
}

void TjcTransport_OnUartError(TjcTransport *transport, UART_HandleTypeDef *uart)
{
    if ((transport == NULL) || (uart != transport->uart))
    {
        return;
    }

    transport->rx_rearm_pending = 1U;
    /* ISR 只通知；前台回调才会中止当前 addt/文本事务。 */
    transport->error_pending = 1U;
}
