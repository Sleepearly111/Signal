#include "bringup_app.h"

#include <stdio.h>

#include "fpga_link_service.h"
#include "main.h"
#include "spi.h"
#include "usart.h"

static FpgaLinkService fpga_link;
static char debug_line[192];
static uint32_t packet_count;
static uint32_t bad_header_count;

static void Debug_Write(const char *text, int length)
{
  if (length > 0)
  {
    if ((size_t)length >= sizeof(debug_line))
    {
      length = (int)sizeof(debug_line) - 1;
    }
    (void)HAL_UART_Transmit(&huart2, (uint8_t *)text, (uint16_t)length, 100U);
  }
}

static uint16_t ReadLe16(const uint8_t *data)
{
  return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static void ReportPacket(const FpgaProtocolPacket *packet)
{
  int length;

  if ((packet->kind == 0x10U) && (packet->payload_length == 8U))
  {
    length = snprintf(debug_line, sizeof(debug_line),
                      "[fpga] OK seq=%u kind=10 len=8 values=%u,%u,%u,%u "
                      "crc=%04X\r\n",
                      packet->sequence,
                      ReadLe16(&packet->payload[0]),
                      ReadLe16(&packet->payload[2]),
                      ReadLe16(&packet->payload[4]),
                      ReadLe16(&packet->payload[6]),
                      packet->received_crc);
  }
  else if ((packet->kind == 0x20U) && (packet->payload_length > 0U))
  {
    uint8_t minimum = packet->payload[0];
    uint8_t maximum = packet->payload[0];
    uint32_t sum = 0U;

    for (uint16_t i = 0U; i < packet->payload_length; ++i)
    {
      uint8_t sample = packet->payload[i];
      if (sample < minimum)
      {
        minimum = sample;
      }
      if (sample > maximum)
      {
        maximum = sample;
      }
      sum += sample;
    }

    uint32_t average_x100 =
        (sum * 100U) / (uint32_t)packet->payload_length;
    length = snprintf(
        debug_line, sizeof(debug_line),
        "[adc] seq=%u n=%u min=%02X max=%02X pp=%u avg=%u.%02u "
        "first=%02X,%02X,%02X,%02X crc=%04X\r\n",
        packet->sequence,
        packet->payload_length,
        minimum,
        maximum,
        (uint16_t)maximum - (uint16_t)minimum,
        (unsigned int)(average_x100 / 100U),
        (unsigned int)(average_x100 % 100U),
        packet->payload[0],
        packet->payload_length > 1U ? packet->payload[1] : 0U,
        packet->payload_length > 2U ? packet->payload[2] : 0U,
        packet->payload_length > 3U ? packet->payload[3] : 0U,
        packet->received_crc);
  }
  else
  {
    length = snprintf(debug_line, sizeof(debug_line),
                      "[fpga] OK seq=%u kind=%02X len=%u first=%02X,%02X,"
                      "%02X,%02X crc=%04X\r\n",
                      packet->sequence,
                      packet->kind,
                      packet->payload_length,
                      packet->payload_length > 0U ? packet->payload[0] : 0U,
                      packet->payload_length > 1U ? packet->payload[1] : 0U,
                      packet->payload_length > 2U ? packet->payload[2] : 0U,
                      packet->payload_length > 3U ? packet->payload[3] : 0U,
                      packet->received_crc);
  }
  Debug_Write(debug_line, length);
}

static void ReportBadHeader(void)
{
  const uint8_t *header = FpgaLinkService_LastHeader(&fpga_link);
  int length = snprintf(
      debug_line, sizeof(debug_line),
      "[fpga] ERROR bad header raw=%02X %02X %02X %02X %02X %02X %02X "
      "%02X %02X; no ACK\r\n",
      header[0], header[1], header[2], header[3], header[4],
      header[5], header[6], header[7], header[8]);

  Debug_Write(debug_line, length);
}

void BringupApp_Init(void)
{
  const FpgaSpiAdapter adapter = {
      .spi = &hspi3,
      .cs_port = FPGA_CS_N_GPIO_Port,
      .cs_pin = FPGA_CS_N_Pin,
      .drdy_port = FPGA_DRDY_GPIO_Port,
      .drdy_pin = FPGA_DRDY_Pin,
  };

  FpgaLinkService_Init(&fpga_link, &adapter);
  Debug_Write("[fpga] SPI3 link service ready\r\n",
              sizeof("[fpga] SPI3 link service ready\r\n") - 1);
}

void BringupApp_Process(void)
{
  FpgaProtocolPacket packet;
  FpgaLinkEvent event;

  FpgaLinkService_Process(&fpga_link, HAL_GetTick());
  event = FpgaLinkService_TakeEvent(&fpga_link, &packet);

  switch (event)
  {
    case FPGA_LINK_EVENT_PACKET:
      ++packet_count;
      if ((packet_count <= 12U) || ((packet_count % 100U) == 0U))
      {
        ReportPacket(&packet);
      }
      break;
    case FPGA_LINK_EVENT_BAD_HEADER:
      ++bad_header_count;
      if ((bad_header_count <= 12U) || ((bad_header_count % 100U) == 0U))
      {
        ReportBadHeader();
      }
      break;
    case FPGA_LINK_EVENT_BAD_CRC:
      Debug_Write("[fpga] ERROR bad CRC; no ACK\r\n",
                  sizeof("[fpga] ERROR bad CRC; no ACK\r\n") - 1);
      break;
    case FPGA_LINK_EVENT_SPI_ERROR:
      Debug_Write("[fpga] ERROR SPI/DMA\r\n",
                  sizeof("[fpga] ERROR SPI/DMA\r\n") - 1);
      break;
    default:
      break;
  }
}

void BringupApp_OnDrdyIrq(uint16_t gpio_pin)
{
  if (gpio_pin == FPGA_DRDY_Pin)
  {
    FpgaLinkService_OnDrdyIrq(&fpga_link);
  }
}

void BringupApp_OnSpiComplete(SPI_HandleTypeDef *spi)
{
  FpgaLinkService_OnSpiComplete(&fpga_link, spi);
}

void BringupApp_OnSpiError(SPI_HandleTypeDef *spi)
{
  FpgaLinkService_OnSpiError(&fpga_link, spi);
}
