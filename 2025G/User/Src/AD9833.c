#include "AD9833.h"
#include "usart.h"
#include <stdio.h>

uint32_t ch0_freqword = 0;
uint32_t ch1_freqword = 0;
uint8_t wave_flag = 1;
uint8_t force = 0; //0：写频率 1： 频率字

#define FCLK         10000000U  /* AD9833 实际时钟 10MHz */
#define RealFreDat   (268435456.0 / FCLK)//总的公式为 Fout=（Fclk/2的28次方）*28位寄存器的值

static void ad9833_write_bit_a(uint8_t bit)
{
    if (bit != 0U) {
        AD9833_A_SDATA_H();
    } else {
        AD9833_A_SDATA_L();
    }
    AD9833_A_SCLK_L();
    AD9833_A_SCLK_H();
}

static void ad9833_write_bit_b(uint8_t bit)
{
    if (bit != 0U) {
        AD9833_B_SDATA_H();
    } else {
        AD9833_B_SDATA_L();
    }
    AD9833_B_SCLK_L();
    AD9833_B_SCLK_H();
}

void set_clock(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();  /* 必须使能 GPIOC 时钟，否则 MCO 不生效 */

    GPIO_InitStructure.Pin = GPIO_PIN_9;
    GPIO_InitStructure.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStructure.Pull = GPIO_NOPULL;
    GPIO_InitStructure.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStructure.Alternate = GPIO_AF0_MCO;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStructure);

    HAL_RCC_MCOConfig(RCC_MCO2, RCC_MCO2SOURCE_HSE, RCC_MCODIV_2);
}

void adjust_channel_freq(char ch, double freq, char type)
{
    wave_flag = (uint8_t)ch;
    if (type == 0) {
        AD9833_SetFrequencyQuick((float)freq, AD9833_OUT_SINUS);
    } else if (type == 1) {
        AD9833_SetFrequencyQuick((float)freq, AD9833_OUT_TRIANGLE);
    }
}

void together_freq(uint32_t freq_A, uint32_t freq_B)
{
    uint16_t freqHi_A = AD9833_REG_FREQ0;
    uint16_t freqLo_A = AD9833_REG_FREQ0;
    uint16_t freqHi_B = AD9833_REG_FREQ0;
    uint16_t freqLo_B = AD9833_REG_FREQ0;

    uint32_t val_A = freq_A;
    uint32_t val_B = freq_B;

    unsigned char data_A[5] = {0x03, 0x00, 0x00};
    unsigned char data_B[5] = {0x03, 0x00, 0x00};

    freqHi_A |= (uint16_t)((val_A & 0xFFFC000UL) >> 14);
    freqLo_A |= (uint16_t)(val_A & 0x3FFFUL);
    freqHi_B |= (uint16_t)((val_B & 0xFFFC000UL) >> 14);
    freqLo_B |= (uint16_t)(val_B & 0x3FFFUL);

    data_A[1] = (unsigned char)(((AD9833_B28 | AD9833_OUT_SINUS) & 0xFF00U) >> 8);
    data_A[2] = (unsigned char)((AD9833_B28 | AD9833_OUT_SINUS) & 0x00FFU);
    data_B[1] = data_A[1];
    data_B[2] = data_A[2];
    together_SPI_Write(data_A, data_B, 2);

    data_A[1] = (unsigned char)((freqLo_A & 0xFF00U) >> 8);
    data_A[2] = (unsigned char)(freqLo_A & 0x00FFU);
    data_B[1] = (unsigned char)((freqLo_B & 0xFF00U) >> 8);
    data_B[2] = (unsigned char)(freqLo_B & 0x00FFU);
    together_SPI_Write(data_A, data_B, 2);

    data_A[1] = (unsigned char)((freqHi_A & 0xFF00U) >> 8);
    data_A[2] = (unsigned char)(freqHi_A & 0x00FFU);
    data_B[1] = (unsigned char)((freqHi_B & 0xFF00U) >> 8);
    data_B[2] = (unsigned char)(freqHi_B & 0x00FFU);
    together_SPI_Write(data_A, data_B, 2);
}

void adjust_channel_phase(char ch, double phase, char type)
{
    uint16_t pha;
    (void)type;
    pha = (uint16_t)((float)phase * 4096.0f / 360.0f);
    wave_flag = (uint8_t)ch;
    AD9833_SetPhase(AD9833_REG_PHASE0, pha);
}

void together_phase(double phase_A, double phase_B)
{
    uint16_t pha_A;
    uint16_t pha_B;
    uint16_t phase_word_A = AD9833_REG_PHASE0;
    uint16_t phase_word_B = AD9833_REG_PHASE0;

    unsigned char data_A[5] = {0x03, 0x00, 0x00};
    unsigned char data_B[5] = {0x03, 0x00, 0x00};

    pha_A = (uint16_t)((float)phase_A * 4096.0f / 360.0f + 0.5f);
    pha_B = (uint16_t)((float)phase_B * 4096.0f / 360.0f + 0.5f);

    printf("phase_word: %u,%u\r\n", pha_A, pha_B);

    phase_word_A |= pha_A;
    phase_word_B |= pha_B;

    data_A[1] = (unsigned char)((phase_word_A & 0xFF00U) >> 8);
    data_A[2] = (unsigned char)(phase_word_A & 0x00FFU);

    data_B[1] = (unsigned char)((phase_word_B & 0xFF00U) >> 8);
    data_B[2] = (unsigned char)(phase_word_B & 0x00FFU);

    together_SPI_Write(data_A, data_B, 2);
}

void together_SPI_Write(unsigned char* data_A, unsigned char* data_B, unsigned char bytesNumber)
{
    unsigned char i;
    unsigned char j;
    unsigned char writeData_A[5] = {0, 0, 0, 0, 0};
    unsigned char writeData_B[5] = {0, 0, 0, 0, 0};

    for (i = 0; i < bytesNumber; i++) {
        writeData_A[i] = data_A[i + 1U];
        writeData_B[i] = data_B[i + 1U];
    }

    AD9833_A_SCLK_H();
    AD9833_B_SCLK_H();
    AD9833_A_FSYNC_L();
    AD9833_B_FSYNC_L();

    for (i = 0; i < bytesNumber; i++) {
        for (j = 0; j < 8U; j++) {
            ad9833_write_bit_a((uint8_t)((writeData_A[i] & 0x80U) != 0U));
            ad9833_write_bit_b((uint8_t)((writeData_B[i] & 0x80U) != 0U));
            writeData_A[i] <<= 1U;
            writeData_B[i] <<= 1U;
        }
    }

    AD9833_A_SDATA_H();
    AD9833_B_SDATA_H();
    AD9833_A_FSYNC_H();
    AD9833_B_FSYNC_H();
}

unsigned char AD9833_SPI_Write(unsigned char* data, unsigned char bytesNumber)
{
    unsigned char i;
    unsigned char j;
    unsigned char writeData[5] = {0, 0, 0, 0, 0};

    for (i = 0; i < bytesNumber; i++) {
        writeData[i] = data[i + 1U];
    }

    if (wave_flag == 0U) {
        AD9833_A_SCLK_H();
        AD9833_A_FSYNC_L();

        for (i = 0; i < bytesNumber; i++) {
            for (j = 0; j < 8U; j++) {
                ad9833_write_bit_a((uint8_t)((writeData[i] & 0x80U) != 0U));
                writeData[i] <<= 1U;
            }
        }

        AD9833_A_SDATA_H();
        AD9833_A_FSYNC_H();
    } else {
        AD9833_B_SCLK_H();
        AD9833_B_FSYNC_L();

        for (i = 0; i < bytesNumber; i++) {
            for (j = 0; j < 8U; j++) {
                ad9833_write_bit_b((uint8_t)((writeData[i] & 0x80U) != 0U));
                writeData[i] <<= 1U;
            }
        }

        AD9833_B_SDATA_H();
        AD9833_B_FSYNC_H();
    }

    return i;
}

void AD9833_Init(void)
{
    HAL_Delay(500);

    AD9833_A_SCLK_H();
    AD9833_B_SCLK_H();
    AD9833_A_FSYNC_H();
    AD9833_B_FSYNC_H();
    AD9833_A_SDATA_H();
    AD9833_B_SDATA_H();

    wave_flag = 0U;
    AD9833_SetRegisterValue(AD9833_REG_CMD | AD9833_RESET);
    AD9833_Reset();

    HAL_Delay(10);

    wave_flag = 1U;
    AD9833_SetRegisterValue(AD9833_REG_CMD | AD9833_RESET);
    AD9833_Reset();

    HAL_Delay(10);

    /* 两个芯片都退出复位，并设成正弦波模式 */
    wave_flag = 0;
    AD9833_SetFrequencyQuick(1000.0f, AD9833_OUT_SINUS);
    wave_flag = 1;
    AD9833_SetFrequencyQuick(1000.0f, AD9833_OUT_SINUS);
}

uint32_t AD9833_GetFreqWord(float hz)
{
    return (uint32_t)(RealFreDat * hz);
}

void AD9833_Reset(void)
{
    AD9833_SetRegisterValue(AD9833_REG_CMD | AD9833_RESET);
    HAL_Delay(10);
}

void AD9833_ClearReset(void)
{
    AD9833_SetRegisterValue(AD9833_REG_CMD);
}

void AD9833_SetRegisterValue(uint16_t regValue)
{
    unsigned char data[5] = {0x03, 0x00, 0x00};

    data[1] = (unsigned char)((regValue & 0xFF00U) >> 8);
    data[2] = (unsigned char)(regValue & 0x00FFU);
    AD9833_SPI_Write(data, 2);
}

void AD9833_SetFrequencyQuick(float fout, uint16_t type)
{
    AD9833_SetFrequency(AD9833_REG_FREQ0, fout, type);
}

void AD9833_SetFrequency(uint16_t reg, float fout, uint16_t type)
{
    uint16_t freqHi = reg;
    uint16_t freqLo = reg;
    uint32_t val = (uint32_t)(RealFreDat * fout);

    if (force == 1U) {
        val = (uint32_t)fout;
    }

    freqHi |= (uint16_t)((val & 0xFFFC000UL) >> 14);
    freqLo |= (uint16_t)(val & 0x3FFFUL);
    AD9833_SetRegisterValue((uint16_t)(AD9833_B28 | type));

    if (force == 0U) {
        ch0_freqword = val;
    }

    AD9833_SetRegisterValue(freqLo);
    AD9833_SetRegisterValue(freqHi);
}

void AD9833_SetPhase(uint16_t reg, uint16_t val)
{
    uint16_t phase = reg;
    phase |= val;
    AD9833_SetRegisterValue(phase);
}

void AD9833_Setup(uint32_t freq, uint16_t phase, uint16_t type)
{
    uint16_t val = (uint16_t)(freq | phase | type);
    AD9833_SetRegisterValue(val);
}

void AD9833_SetWave(uint16_t type)
{
    AD9833_SetRegisterValue(type);
}
