#ifndef FPGA_LINK_SERVICE_H
#define FPGA_LINK_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "fpga_protocol.h"
#include "fpga_spi_adapter.h"

typedef enum
{
  FPGA_LINK_IDLE = 0,
  FPGA_LINK_READING_HEADER,
  FPGA_LINK_READING_PAYLOAD,
  FPGA_LINK_SENDING_ACK
} FpgaLinkState;

typedef enum
{
  FPGA_LINK_EVENT_NONE = 0,
  FPGA_LINK_EVENT_PACKET,
  FPGA_LINK_EVENT_BAD_HEADER,
  FPGA_LINK_EVENT_BAD_CRC,
  FPGA_LINK_EVENT_SPI_ERROR
} FpgaLinkEvent;

typedef struct
{
  FpgaSpiAdapter adapter;
  FpgaLinkState state;
  volatile bool drdy_irq;
  volatile bool transfer_complete;
  volatile bool transfer_error;
  uint32_t next_attempt_ms;
  uint8_t header_tx[10];
  uint8_t header_rx[10];
  uint8_t payload_tx[FPGA_PROTOCOL_MAX_PAYLOAD + 2U];
  uint8_t payload_rx[FPGA_PROTOCOL_MAX_PAYLOAD + 2U];
  uint8_t ack_tx[3];
  uint8_t ack_rx[3];
  uint8_t header[FPGA_PROTOCOL_HEADER_SIZE];
  uint16_t payload_length;
  FpgaProtocolPacket packet;
  FpgaLinkEvent pending_event;
} FpgaLinkService;

void FpgaLinkService_Init(FpgaLinkService *service,
                          const FpgaSpiAdapter *adapter);
void FpgaLinkService_Process(FpgaLinkService *service, uint32_t now_ms);
void FpgaLinkService_OnDrdyIrq(FpgaLinkService *service);
void FpgaLinkService_OnSpiComplete(FpgaLinkService *service,
                                   SPI_HandleTypeDef *spi);
void FpgaLinkService_OnSpiError(FpgaLinkService *service,
                                SPI_HandleTypeDef *spi);
FpgaLinkEvent FpgaLinkService_TakeEvent(FpgaLinkService *service,
                                        FpgaProtocolPacket *packet);
const uint8_t *FpgaLinkService_LastHeader(const FpgaLinkService *service);

#endif
