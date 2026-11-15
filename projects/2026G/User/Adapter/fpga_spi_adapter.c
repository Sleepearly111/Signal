#include "fpga_spi_adapter.h"

static void FpgaSpiAdapter_ChipSelectDelay(void)
{
  for (volatile uint32_t delay = 0U; delay < 128U; ++delay)
  {
    __NOP();
  }
}

void FpgaSpiAdapter_Init(FpgaSpiAdapter *adapter)
{
  if (adapter != NULL)
  {
    HAL_GPIO_WritePin(adapter->cs_port, adapter->cs_pin, GPIO_PIN_SET);
  }
}

bool FpgaSpiAdapter_IsReady(const FpgaSpiAdapter *adapter)
{
  return (adapter != NULL) &&
         (HAL_GPIO_ReadPin(adapter->drdy_port, adapter->drdy_pin) ==
          GPIO_PIN_SET);
}

void FpgaSpiAdapter_Select(const FpgaSpiAdapter *adapter)
{
  HAL_GPIO_WritePin(adapter->cs_port, adapter->cs_pin, GPIO_PIN_RESET);
  FpgaSpiAdapter_ChipSelectDelay();
}

void FpgaSpiAdapter_Deselect(const FpgaSpiAdapter *adapter)
{
  HAL_GPIO_WritePin(adapter->cs_port, adapter->cs_pin, GPIO_PIN_SET);
  FpgaSpiAdapter_ChipSelectDelay();
}

HAL_StatusTypeDef FpgaSpiAdapter_TransferDma(
    const FpgaSpiAdapter *adapter,
    uint8_t *tx,
    uint8_t *rx,
    uint16_t length)
{
  if ((adapter == NULL) || (adapter->spi == NULL))
  {
    return HAL_ERROR;
  }
  return HAL_SPI_TransmitReceive_DMA(adapter->spi, tx, rx, length);
}

void FpgaSpiAdapter_Abort(const FpgaSpiAdapter *adapter)
{
  if ((adapter != NULL) && (adapter->spi != NULL))
  {
    (void)HAL_SPI_Abort(adapter->spi);
    FpgaSpiAdapter_Deselect(adapter);
  }
}
