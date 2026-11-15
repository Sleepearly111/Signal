#ifndef FPGA_SPI_ADAPTER_H
#define FPGA_SPI_ADAPTER_H

#include <stdbool.h>
#include <stdint.h>

#include "stm32f4xx_hal.h"

typedef struct
{
  SPI_HandleTypeDef *spi;
  GPIO_TypeDef *cs_port;
  uint16_t cs_pin;
  GPIO_TypeDef *drdy_port;
  uint16_t drdy_pin;
} FpgaSpiAdapter;

void FpgaSpiAdapter_Init(FpgaSpiAdapter *adapter);
bool FpgaSpiAdapter_IsReady(const FpgaSpiAdapter *adapter);
void FpgaSpiAdapter_Select(const FpgaSpiAdapter *adapter);
void FpgaSpiAdapter_Deselect(const FpgaSpiAdapter *adapter);
HAL_StatusTypeDef FpgaSpiAdapter_TransferDma(
    const FpgaSpiAdapter *adapter,
    uint8_t *tx,
    uint8_t *rx,
    uint16_t length);
void FpgaSpiAdapter_Abort(const FpgaSpiAdapter *adapter);

#endif
