#ifndef __ADS8688_H
#define __ADS8688_H

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ADS8688_OK = 0,
    ADS8688_ERR_INIT,
    ADS8688_ERR_AUTO_SEQ,
    ADS8688_ERR_POWER_DOWN,
    ADS8688_ERR_RANGE_CH0,
    ADS8688_ERR_RANGE_CH1
} ADS8688_Status_t;

extern volatile uint8_t ads8688_sample_request;
extern volatile uint8_t ads8688_data_ready;
extern volatile uint8_t ads8688_fault;
extern volatile uint16_t v_suoxiang[2];

ADS8688_Status_t ads8688_set(void);//CH0 CH1进入自动扫描
void ADS8688_Service(void);
uint8_t ADS8688_BeginAccess(void);
void ADS8688_EndAccess(void);
float ADS8688_CodeToMilliVolt(uint16_t code);

void Delay(uint32_t nCount);
void ADS8688A_SPI_WB(uint8_t com);
uint8_t ADS8688A_SPI_RB(void);
uint8_t ADS8688A_INIT(void);
void ADS8688A_WriteCommandReg(uint16_t command);//写ADS8688命令寄存器
uint8_t ADS8688A_READ_Program_Register(uint8_t Addr);
uint16_t Get_MAN_Ch_n_Mode_Data(void);
void MAN_Ch_n_Mode(uint16_t ch);//选择输入通道
void Set_CH_Range_Select(uint8_t ch, uint8_t range);//设置各个通道的范围
void ADS8688A_Write_Program_Register(uint8_t Addr, uint8_t data);
void AUTO_RST_Mode(void);
void Get_AUTO_RST_Mode_Data(uint16_t* outputdata, uint8_t chnum);

// Command Register (datasheet P43, Table 6. Command Register Map)
#define ADS8688_CMD_NO_OP      0x0000U
#define ADS8688_CMD_STDBY      0x8200U
#define ADS8688_CMD_PWR_DN     0x8300U
#define ADS8688_CMD_RST        0x8500U
#define ADS8688_CMD_AUTO_RST   0xA000U
#define ADS8688_CMD_MAN_CH_0   0xC000U
#define ADS8688_CMD_MAN_CH_1   0xC400U
#define ADS8688_CMD_MAN_CH_2   0xC800U
#define ADS8688_CMD_MAN_CH_3   0xCC00U
#define ADS8688_CMD_MAN_CH_4   0xD000U
#define ADS8688_CMD_MAN_CH_5   0xD400U
#define ADS8688_CMD_MAN_CH_6   0xD800U
#define ADS8688_CMD_MAN_CH_7   0xDC00U
#define ADS8688_CMD_MAN_AUX    0xE000U

// Backward-compatible command aliases
#define NO_OP      ADS8688_CMD_NO_OP
#define STDBY      ADS8688_CMD_STDBY
#define PWR_DN     ADS8688_CMD_PWR_DN
#define RST        ADS8688_CMD_RST
#define AUTO_RST   ADS8688_CMD_AUTO_RST
#define MAN_Ch_0   ADS8688_CMD_MAN_CH_0
#define MAN_Ch_1   ADS8688_CMD_MAN_CH_1
#define MAN_Ch_2   ADS8688_CMD_MAN_CH_2
#define MAN_Ch_3   ADS8688_CMD_MAN_CH_3
#define MAN_Ch_4   ADS8688_CMD_MAN_CH_4
#define MAN_Ch_5   ADS8688_CMD_MAN_CH_5
#define MAN_Ch_6   ADS8688_CMD_MAN_CH_6
#define MAN_Ch_7   ADS8688_CMD_MAN_CH_7
#define MAN_AUX    ADS8688_CMD_MAN_AUX

/*
 * ADS8688 software SPI / control pins (CubeMX labels)
 * PD0 -> ADS8688_RST
 * PD1 -> ADS8688_CONVST
 * PD2 -> ADS8688_CS
 * PD3 -> ADS8688_SCLK
 * PD4 -> ADS8688_SDI
 * PD5 -> ADS8688_SDO
 */
/* 直接寄存器操作 BSRR/IDR，比 HAL GPIO 快 ~30-50 倍 */
#define ADS8688_RST_H()      (ADS8688_RST_GPIO_Port->BSRR = ADS8688_RST_Pin)
#define ADS8688_RST_L()      (ADS8688_RST_GPIO_Port->BSRR = (uint32_t)ADS8688_RST_Pin << 16)
#define ADS8688_DAISY_H()    (ADS8688_DAISY_GPIO_Port->BSRR = ADS8688_DAISY_Pin)
#define ADS8688_DAISY_L()    (ADS8688_DAISY_GPIO_Port->BSRR = (uint32_t)ADS8688_DAISY_Pin << 16)
#define ADS8688_CS_H()       (ADS8688_CS_GPIO_Port->BSRR = ADS8688_CS_Pin)
#define ADS8688_CS_L()       (ADS8688_CS_GPIO_Port->BSRR = (uint32_t)ADS8688_CS_Pin << 16)
#define ADS8688_SCLK_H()     (ADS8688_SCLK_GPIO_Port->BSRR = ADS8688_SCLK_Pin)
#define ADS8688_SCLK_L()     (ADS8688_SCLK_GPIO_Port->BSRR = (uint32_t)ADS8688_SCLK_Pin << 16)
#define ADS8688_SDI_H()      (ADS8688_SDI_GPIO_Port->BSRR = ADS8688_SDI_Pin)
#define ADS8688_SDI_L()      (ADS8688_SDI_GPIO_Port->BSRR = (uint32_t)ADS8688_SDI_Pin << 16)
#define ADS8688_SDO_READ()   ((ADS8688_SDO_GPIO_Port->IDR & ADS8688_SDO_Pin) ? GPIO_PIN_SET : GPIO_PIN_RESET)

// Backward-compatible pin aliases
#define RST_PD_H   ADS8688_RST_H()
#define RST_PD_L   ADS8688_RST_L()
#define DAISY_IN_H ADS8688_DAISY_H()
#define DAISY_IN_L ADS8688_DAISY_L()
#define nCS_H      ADS8688_CS_H()
#define nCS_L      ADS8688_CS_L()
#define SCLK_H     ADS8688_SCLK_H()
#define SCLK_L     ADS8688_SCLK_L()
#define SDI_H      ADS8688_SDI_H()
#define SDI_L      ADS8688_SDI_L()
#define SDO        ADS8688_SDO_READ()

#ifdef __cplusplus
}
#endif

#endif
