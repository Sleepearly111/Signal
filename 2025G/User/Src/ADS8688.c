#include "ADS8688.h"

uint8_t test;

volatile uint8_t ads8688_sample_request = 0U;
volatile uint8_t ads8688_data_ready = 0U;
volatile uint8_t ads8688_fault = ADS8688_OK;
volatile uint16_t v_suoxiang[2] = {0U, 0U};

static uint8_t ads8688_busy = 0U;

#define ADS8688_AUTO_SEQ_REG      0x01U
#define ADS8688_POWER_DOWN_REG    0x02U
#define ADS8688_RANGE_CH0_REG     0x05U
#define ADS8688_RANGE_CH1_REG     0x06U
#define ADS8688_AUTO_SEQ_CH0_CH1  0x03U
#define ADS8688_POWER_DOWN_CH2_7  0xFCU
#define ADS8688_RANGE_PM_5V12     0x01U
#define ADS8688_RANGE_PM_10V24     0x00U

static void Enter_RESET_MODE(void)
{
    ADS8688A_WriteCommandReg(RST);
}

static ADS8688_Status_t ADS8688_CheckRegister(uint8_t addr,
                                              uint8_t expected,
                                              ADS8688_Status_t error)
{
    if (ADS8688A_READ_Program_Register(addr) != expected) {
        return error;
    }

    return ADS8688_OK;
}

ADS8688_Status_t ads8688_set(void)
{
    ADS8688_Status_t status = ADS8688_OK;

    ads8688_sample_request = 0U;
    ads8688_data_ready = 0U;
    ads8688_fault = ADS8688_OK;

    if (ADS8688_BeginAccess() == 0U) {
        ads8688_fault = ADS8688_ERR_INIT;
        return ADS8688_ERR_INIT;
    }

    RST_PD_H;
    DAISY_IN_L;
    Delay(0x1FFFU);

    test = ADS8688A_INIT();
    Delay(0x1FFFU);
    if (test != 0xFFU) {
        status = ADS8688_ERR_INIT;
        goto done;
    }

    ADS8688A_Write_Program_Register(ADS8688_POWER_DOWN_REG, ADS8688_POWER_DOWN_CH2_7);
    Delay(0x100U);
    status = ADS8688_CheckRegister(ADS8688_POWER_DOWN_REG,
                                   ADS8688_POWER_DOWN_CH2_7,
                                   ADS8688_ERR_POWER_DOWN);
    if (status != ADS8688_OK) {
        goto done;
    }

    ADS8688A_Write_Program_Register(ADS8688_AUTO_SEQ_REG, ADS8688_AUTO_SEQ_CH0_CH1);
    Delay(0x100U);
    status = ADS8688_CheckRegister(ADS8688_AUTO_SEQ_REG,
                                   ADS8688_AUTO_SEQ_CH0_CH1,
                                   ADS8688_ERR_AUTO_SEQ);
    if (status != ADS8688_OK) {
        goto done;
    }

    Set_CH_Range_Select(ADS8688_RANGE_CH0_REG, ADS8688_RANGE_PM_5V12);
    Delay(0x100U);
    status = ADS8688_CheckRegister(ADS8688_RANGE_CH0_REG,
                                   ADS8688_RANGE_PM_5V12,
                                   ADS8688_ERR_RANGE_CH0);
    if (status != ADS8688_OK) {
        goto done;
    }

    Set_CH_Range_Select(ADS8688_RANGE_CH1_REG, ADS8688_RANGE_PM_5V12);
    Delay(0x100U);
    status = ADS8688_CheckRegister(ADS8688_RANGE_CH1_REG,
                                   ADS8688_RANGE_PM_5V12,
                                   ADS8688_ERR_RANGE_CH1);
    if (status != ADS8688_OK) {
        goto done;
    }

    AUTO_RST_Mode();

done:
    ADS8688_EndAccess();
    ads8688_fault = (uint8_t)status;
    return status;
}

uint8_t ADS8688_BeginAccess(void)
{
    if (ads8688_fault != ADS8688_OK) {
        return 0U;
    }

    if (ads8688_busy != 0U) {
        return 0U;
    }

    ads8688_busy = 1U;
    return 1U;
}

void ADS8688_EndAccess(void)
{
    ads8688_busy = 0U;
}

float ADS8688_CodeToMilliVolt(uint16_t code)
{
    return ((float)code - 32768.0f) / 32768.0f * 5120.0f;
}

void ADS8688_Service(void)
{
    uint16_t sample[2];

    if (ads8688_fault != ADS8688_OK) {
        return;
    }

    if (ads8688_sample_request == 0U) {
        return;
    }

    __disable_irq();
    ads8688_sample_request = 0U;
    __enable_irq();

    if (ADS8688_BeginAccess() == 0U) {
        return;
    }

    Get_AUTO_RST_Mode_Data(sample, 2U);

    __disable_irq();
    v_suoxiang[0] = sample[0];
    v_suoxiang[1] = sample[1];
    ads8688_data_ready = 1U;
    __enable_irq();

    ADS8688_EndAccess();
}

void AUTO_RST_Mode(void)
{
    ADS8688A_WriteCommandReg(AUTO_RST);
}

void Get_AUTO_RST_Mode_Data(uint16_t* outputdata, uint8_t chnum)
{
    if (outputdata == 0) {
        return;
    }

    for (uint8_t i = 0U; i < chnum; i++) {
        uint8_t datah;
        uint8_t datal;

        nCS_L;
        ADS8688A_SPI_WB(0x00U);
        ADS8688A_SPI_WB(0x00U);
        datah = ADS8688A_SPI_RB();
        datal = ADS8688A_SPI_RB();
        nCS_H;

        outputdata[i] = (uint16_t)(((uint16_t)datah << 8) | datal);
    }
}

uint16_t Get_MAN_Ch_n_Mode_Data(void)
{
    uint8_t datah;
    uint8_t datal;

    nCS_L;
    ADS8688A_SPI_WB(0x00U);
    ADS8688A_SPI_WB(0x00U);
    datah = ADS8688A_SPI_RB();
    datal = ADS8688A_SPI_RB();
    nCS_H;

    return (uint16_t)(((uint16_t)datah << 8) | datal);
}

void MAN_Ch_n_Mode(uint16_t ch)
{
    ADS8688A_WriteCommandReg(ch);
}

void ADS8688A_Write_Program_Register(uint8_t Addr, uint8_t data)
{
    nCS_L;
    ADS8688A_SPI_WB((uint8_t)((Addr << 1U) | 0x01U));
    ADS8688A_SPI_WB(data);
    nCS_H;
}

void Set_CH_Range_Select(uint8_t ch, uint8_t range)
{
    ADS8688A_Write_Program_Register(ch, range);
}

uint8_t ADS8688A_READ_Program_Register(uint8_t Addr)
{
    uint8_t data;

    nCS_L;
    ADS8688A_SPI_WB((uint8_t)(Addr << 1U));
    (void)ADS8688A_SPI_RB();
    data = ADS8688A_SPI_RB();
    nCS_H;

    return data;
}

void ADS8688A_WriteCommandReg(uint16_t command)
{
    nCS_L;
    ADS8688A_SPI_WB((uint8_t)((command >> 8) & 0xFFU));
    ADS8688A_SPI_WB((uint8_t)(command & 0xFFU));
    nCS_H;
}

uint8_t ADS8688A_INIT(void)
{
    uint8_t i;

    Delay(0x1FFFU);

    Enter_RESET_MODE();
    ADS8688A_Write_Program_Register(ADS8688_AUTO_SEQ_REG, 0xFFU);
    i = ADS8688A_READ_Program_Register(ADS8688_AUTO_SEQ_REG);

    return i;
}

/* SPI 时序延迟: ~5 NOP ≈ 30ns@168MHz，SCLK ≈ 12.5MHz < ADS8688 上限 17MHz */
#define SPI_DELAY() do { __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); } while(0)

uint8_t ADS8688A_SPI_RB(void)
{
    uint8_t rdata = 0U;
    uint8_t s;

    for (s = 0U; s < 8U; s++) {
        rdata <<= 1U;
        SCLK_H;
        SPI_DELAY();
        if (SDO == GPIO_PIN_SET) {
            rdata |= 0x01U;
        }
        SCLK_L;
        SPI_DELAY();
    }

    return rdata;
}

void ADS8688A_SPI_WB(uint8_t com)
{
    uint8_t com_temp = com;
    uint8_t s;

    nCS_L;
    for (s = 0U; s < 8U; s++) {
        if ((com_temp & 0x80U) != 0U) {
            SDI_H;
        } else {
            SDI_L;
        }
        SCLK_H;
        SPI_DELAY();
        com_temp <<= 1U;
        SCLK_L;
        SPI_DELAY();
    }
}

void Delay(uint32_t nCount)
{
    volatile uint32_t count = nCount;
    while (count-- != 0U) {
        __NOP();
    }
}
