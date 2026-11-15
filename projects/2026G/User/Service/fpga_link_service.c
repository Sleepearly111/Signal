#include "fpga_link_service.h"

#include <string.h>

#define FPGA_READ_COMMAND 0xA1U
#define FPGA_ACK_COMMAND 0xA2U
#define FPGA_RETRY_DELAY_MS 100U
#define FPGA_POST_ACK_SETTLE_MS 2U
#define FPGA_TRANSFER_TIMEOUT_MS 100U

static bool FpgaLinkService_DeadlineReached(uint32_t now_ms,
                                            uint32_t deadline_ms)
{
  return (int32_t)(now_ms - deadline_ms) >= 0;
}

static void FpgaLinkService_Fail(FpgaLinkService *service,
                                 FpgaLinkEvent event,
                                 uint32_t now_ms)
{
  FpgaSpiAdapter_Abort(&service->adapter);
  service->state = FPGA_LINK_IDLE;
  service->transfer_complete = false;
  service->transfer_error = false;
  service->drdy_irq = false;
  service->transfer_deadline_ms = 0U;
  service->pending_event = event;
  service->next_attempt_ms = now_ms + FPGA_RETRY_DELAY_MS;
}

static bool FpgaLinkService_StartHeader(FpgaLinkService *service,
                                        uint32_t now_ms)
{
  memset(service->header_tx, 0, sizeof(service->header_tx));
  memset(service->header_rx, 0, sizeof(service->header_rx));
  service->header_tx[0] = FPGA_READ_COMMAND;
  service->transfer_complete = false;
  service->transfer_error = false;
  FpgaSpiAdapter_Select(&service->adapter);

  if (FpgaSpiAdapter_TransferDma(&service->adapter,
                                 service->header_tx,
                                 service->header_rx,
                                 sizeof(service->header_tx)) != HAL_OK)
  {
    FpgaSpiAdapter_Deselect(&service->adapter);
    return false;
  }

  service->state = FPGA_LINK_READING_HEADER;
  service->transfer_deadline_ms =
      now_ms + FPGA_TRANSFER_TIMEOUT_MS;
  return true;
}

void FpgaLinkService_Init(FpgaLinkService *service,
                          const FpgaSpiAdapter *adapter)
{
  memset(service, 0, sizeof(*service));
  service->adapter = *adapter;
  service->state = FPGA_LINK_IDLE;
  FpgaSpiAdapter_Init(&service->adapter);
}

void FpgaLinkService_Process(FpgaLinkService *service, uint32_t now_ms)
{
  if (service->transfer_error)
  {
    FpgaLinkService_Fail(service, FPGA_LINK_EVENT_SPI_ERROR, now_ms);
    return;
  }
  if (((service->state == FPGA_LINK_READING_HEADER) ||
       (service->state == FPGA_LINK_READING_PAYLOAD) ||
       (service->state == FPGA_LINK_SENDING_ACK)) &&
      !service->transfer_complete &&
      FpgaLinkService_DeadlineReached(
          now_ms, service->transfer_deadline_ms))
  {
    FpgaLinkService_Fail(service, FPGA_LINK_EVENT_TIMEOUT, now_ms);
    return;
  }

  switch (service->state)
  {
    case FPGA_LINK_IDLE:
      if (((int32_t)(now_ms - service->next_attempt_ms) >= 0) &&
          (service->drdy_irq ||
           FpgaSpiAdapter_IsReady(&service->adapter)))
      {
        service->drdy_irq = false;
        if (!FpgaLinkService_StartHeader(service, now_ms))
        {
          FpgaLinkService_Fail(service, FPGA_LINK_EVENT_SPI_ERROR, now_ms);
        }
      }
      break;

    case FPGA_LINK_READING_HEADER:
      if (service->transfer_complete)
      {
        service->transfer_complete = false;
        memcpy(service->header, &service->header_rx[1],
               FPGA_PROTOCOL_HEADER_SIZE);
        if (!FpgaProtocol_ParseHeader(service->header,
                                      &service->payload_length))
        {
          FpgaLinkService_Fail(service, FPGA_LINK_EVENT_BAD_HEADER, now_ms);
          break;
        }

        memset(service->payload_tx, 0,
               service->payload_length + 2U);
        if (FpgaSpiAdapter_TransferDma(&service->adapter,
                                       service->payload_tx,
                                       service->payload_rx,
                                       service->payload_length + 2U) != HAL_OK)
        {
          FpgaLinkService_Fail(service, FPGA_LINK_EVENT_SPI_ERROR, now_ms);
          break;
        }
        service->state = FPGA_LINK_READING_PAYLOAD;
        service->transfer_deadline_ms =
            now_ms + FPGA_TRANSFER_TIMEOUT_MS;
      }
      break;

    case FPGA_LINK_READING_PAYLOAD:
      if (service->transfer_complete)
      {
        service->transfer_complete = false;
        FpgaSpiAdapter_Deselect(&service->adapter);
        if (!FpgaProtocol_Decode(service->header,
                                 service->payload_rx,
                                 service->payload_length,
                                 &service->packet))
        {
          FpgaLinkService_Fail(service, FPGA_LINK_EVENT_BAD_CRC, now_ms);
          break;
        }

        /*
         * Do not ACK here.  The owner must first validate and copy the
         * packet into its acquisition window, then call AcceptPacket().
         * This preserves FPGA ownership of the frozen chunk if application
         * validation fails or the consumer is not ready.
         */
        service->state = FPGA_LINK_WAITING_PACKET_DECISION;
        service->pending_event = FPGA_LINK_EVENT_PACKET;
      }
      break;

    case FPGA_LINK_WAITING_PACKET_DECISION:
      break;

    case FPGA_LINK_SENDING_ACK:
      if (service->transfer_complete)
      {
        service->transfer_complete = false;
        FpgaSpiAdapter_Deselect(&service->adapter);
        service->state = FPGA_LINK_IDLE;
        service->transfer_deadline_ms = 0U;
        service->next_attempt_ms = now_ms + FPGA_POST_ACK_SETTLE_MS;
        service->pending_event = FPGA_LINK_EVENT_PACKET_ACKED;
      }
      break;

    default:
      FpgaLinkService_Fail(service, FPGA_LINK_EVENT_SPI_ERROR, now_ms);
      break;
  }
}

void FpgaLinkService_OnDrdyIrq(FpgaLinkService *service)
{
  service->drdy_irq = true;
}

void FpgaLinkService_OnSpiComplete(FpgaLinkService *service,
                                   SPI_HandleTypeDef *spi)
{
  if ((spi == service->adapter.spi) &&
      (service->state != FPGA_LINK_IDLE))
  {
    service->transfer_complete = true;
  }
}

void FpgaLinkService_OnSpiError(FpgaLinkService *service,
                                SPI_HandleTypeDef *spi)
{
  if (spi == service->adapter.spi)
  {
    service->transfer_error = true;
  }
}

FpgaLinkEvent FpgaLinkService_TakeEvent(FpgaLinkService *service,
                                        FpgaProtocolPacket *packet)
{
  if (service == NULL)
  {
    return FPGA_LINK_EVENT_NONE;
  }

  FpgaLinkEvent event = service->pending_event;

  if ((event == FPGA_LINK_EVENT_PACKET) && (packet != NULL))
  {
    *packet = service->packet;
  }
  service->pending_event = FPGA_LINK_EVENT_NONE;
  return event;
}

bool FpgaLinkService_AcceptPacket(FpgaLinkService *service,
                                  uint32_t now_ms)
{
  if ((service == NULL) ||
      (service->state != FPGA_LINK_WAITING_PACKET_DECISION))
  {
    return false;
  }

  service->ack_tx[0] = FPGA_ACK_COMMAND;
  service->ack_tx[1] = (uint8_t)service->packet.sequence;
  service->ack_tx[2] = (uint8_t)(service->packet.sequence >> 8);
  memset(service->ack_rx, 0, sizeof(service->ack_rx));
  service->transfer_complete = false;
  service->transfer_error = false;
  FpgaSpiAdapter_Select(&service->adapter);
  if (FpgaSpiAdapter_TransferDma(&service->adapter,
                                 service->ack_tx,
                                 service->ack_rx,
                                 sizeof(service->ack_tx)) != HAL_OK)
  {
    FpgaLinkService_Fail(
        service, FPGA_LINK_EVENT_SPI_ERROR, now_ms);
    return false;
  }

  service->state = FPGA_LINK_SENDING_ACK;
  service->transfer_deadline_ms =
      now_ms + FPGA_TRANSFER_TIMEOUT_MS;
  return true;
}

bool FpgaLinkService_RejectPacket(FpgaLinkService *service,
                                  uint32_t now_ms)
{
  if ((service == NULL) ||
      (service->state != FPGA_LINK_WAITING_PACKET_DECISION))
  {
    return false;
  }

  service->state = FPGA_LINK_IDLE;
  service->pending_event = FPGA_LINK_EVENT_NONE;
  service->drdy_irq = false;
  service->transfer_deadline_ms = 0U;
  service->next_attempt_ms = now_ms + FPGA_RETRY_DELAY_MS;
  return true;
}

const uint8_t *FpgaLinkService_LastHeader(const FpgaLinkService *service)
{
  return service != NULL ? service->header : NULL;
}
