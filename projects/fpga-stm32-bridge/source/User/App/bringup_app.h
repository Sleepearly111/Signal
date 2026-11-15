#ifndef BRINGUP_APP_H
#define BRINGUP_APP_H

#include "stm32f4xx_hal.h"

void BringupApp_Init(void);
void BringupApp_Process(void);
void BringupApp_OnDrdyIrq(uint16_t gpio_pin);
void BringupApp_OnSpiComplete(SPI_HandleTypeDef *spi);
void BringupApp_OnSpiError(SPI_HandleTypeDef *spi);

#endif
