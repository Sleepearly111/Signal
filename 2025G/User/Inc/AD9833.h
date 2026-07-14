#ifndef _AD9833_H_
#define _AD9833_H_

#include "main.h"


#ifdef __cplusplus
extern "C" {
#endif

/* 寄存器 */
#define AD9833_REG_CMD      (0U << 14)
#define AD9833_REG_FREQ0    (1U << 14)
#define AD9833_REG_FREQ1    (2U << 14)
#define AD9833_REG_PHASE0   (6U << 13)
#define AD9833_REG_PHASE1   (7U << 13)

/* 命令控制位 */
#define AD9833_B28          (1U << 13)
#define AD9833_HLB          (1U << 12)
#define AD9833_FSEL0        (0U << 11)
#define AD9833_FSEL1        (1U << 11)
#define AD9833_PSEL0        (0U << 10)
#define AD9833_PSEL1        (1U << 10)
#define AD9833_PIN_SW       (1U << 9)
#define AD9833_RESET        (1U << 8)
#define AD9833_SLEEP1       (1U << 7)
#define AD9833_SLEEP12      (1U << 6)
#define AD9833_OPBITEN      (1U << 5)
#define AD9833_SIGN_PIB     (1U << 4)
#define AD9833_DIV2         (1U << 3)
#define AD9833_MODE         (1U << 1)

#define AD9833_OUT_SINUS    ((0U << 5) | (0U << 1) | (0U << 3))
#define AD9833_OUT_TRIANGLE ((0U << 5) | (1U << 1) | (0U << 3))
#define AD9833_OUT_MSB      ((1U << 5) | (0U << 1) | (1U << 3))
#define AD9833_OUT_MSB2     ((1U << 5) | (0U << 1) | (0U << 3))

#define AD9833_A_FSYNC_H()  HAL_GPIO_WritePin(AD9833A_FSYNC_GPIO_Port, AD9833A_FSYNC_Pin, GPIO_PIN_SET)
#define AD9833_A_FSYNC_L()  HAL_GPIO_WritePin(AD9833A_FSYNC_GPIO_Port, AD9833A_FSYNC_Pin, GPIO_PIN_RESET)
#define AD9833_A_SCLK_H()   HAL_GPIO_WritePin(AD9833A_SCLK_GPIO_Port, AD9833A_SCLK_Pin, GPIO_PIN_SET)
#define AD9833_A_SCLK_L()   HAL_GPIO_WritePin(AD9833A_SCLK_GPIO_Port, AD9833A_SCLK_Pin, GPIO_PIN_RESET)
#define AD9833_A_SDATA_H()  HAL_GPIO_WritePin(AD9833A_SDATA_GPIO_Port, AD9833A_SDATA_Pin, GPIO_PIN_SET)
#define AD9833_A_SDATA_L()  HAL_GPIO_WritePin(AD9833A_SDATA_GPIO_Port, AD9833A_SDATA_Pin, GPIO_PIN_RESET)

#define AD9833_B_FSYNC_H()  HAL_GPIO_WritePin(AD9833B_FSYNC_GPIO_Port, AD9833B_FSYNC_Pin, GPIO_PIN_SET)
#define AD9833_B_FSYNC_L()  HAL_GPIO_WritePin(AD9833B_FSYNC_GPIO_Port, AD9833B_FSYNC_Pin, GPIO_PIN_RESET)
#define AD9833_B_SCLK_H()   HAL_GPIO_WritePin(AD9833B_SCLK_GPIO_Port, AD9833B_SCLK_Pin, GPIO_PIN_SET)
#define AD9833_B_SCLK_L()   HAL_GPIO_WritePin(AD9833B_SCLK_GPIO_Port, AD9833B_SCLK_Pin, GPIO_PIN_RESET)
#define AD9833_B_SDATA_H()  HAL_GPIO_WritePin(AD9833B_SDATA_GPIO_Port, AD9833B_SDATA_Pin, GPIO_PIN_SET)
#define AD9833_B_SDATA_L()  HAL_GPIO_WritePin(AD9833B_SDATA_GPIO_Port, AD9833B_SDATA_Pin, GPIO_PIN_RESET)

void AD9833_Init(void);//初始化IO口及寄存器
void AD9833_Reset(void);//置位AD9833的复位位
void AD9833_ClearReset(void);//清除AD9833的复位位

uint8_t AD9833_SPI_Write(uint8_t* data, uint8_t bytesNumber);//通过SPI接口写入数据，data为要写入的数据，bytesNumber为数据的字节数
void AD9833_SetRegisterValue(uint16_t regValue);//将值写入寄存器

void AD9833_SetFrequency(uint16_t reg, float fout, uint16_t type);//写入频率寄存器
void AD9833_SetPhase(uint16_t reg, uint16_t val);//写入相位寄存器
void AD9833_SetWave(uint16_t type);//设置要输出的波形类型

void AD9833_Setup(uint32_t freq, uint16_t phase, uint16_t type);//选择频率、相位和波形类型
void AD9833_SetFrequencyQuick(float fout, uint16_t type);//设置频率及波形类型


void adjust_channel_freq(char ch, double freq, char type);
void adjust_channel_phase(char ch, double phase, char type);

//A/B 两通道同步设置
void together_freq(uint32_t freq_A, uint32_t freq_B);
void together_phase(double phase_A, double phase_B);
void together_SPI_Write(uint8_t* data_A, uint8_t* data_B, uint8_t bytesNumber);

extern uint8_t force;     //0:写频率 1:写频率字
extern uint8_t wave_flag;  //0:A路 1:B路

void set_clock(void);//配置PC9(MCO2)并输出时钟
uint32_t AD9833_GetFreqWord(float hz);//频率值→频率字

#ifdef __cplusplus
}
#endif

#endif