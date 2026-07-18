#include "ADS8688.h"
#include "spi.h"

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
#define ADS8688_RANGE_CH3_REG     0x08U
#define ADS8688_AUTO_SEQ_CH0_CH3  0x09U  /* CH0(丝印CH1) + CH3(丝印CH4) */
#define ADS8688_POWER_DOWN_CH2_7  0xFCU  /* CH0/CH1 使能, CH2~7 断电 */
#define ADS8688_RANGE_PM_5V12     0x71U  /* bits[3:0]=1 ±5.12V, bits[6:4]=111 LPF旁路 */

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
    if ((ADS8688A_READ_Program_Register(ADS8688_RANGE_CH0_REG) & 0x0FU)
        != (ADS8688_RANGE_PM_5V12 & 0x0FU)) {
        status = ADS8688_ERR_RANGE_CH0;
        goto done;
    }

    Set_CH_Range_Select(ADS8688_RANGE_CH1_REG, ADS8688_RANGE_PM_5V12);
    Delay(0x100U);
    if ((ADS8688A_READ_Program_Register(ADS8688_RANGE_CH1_REG) & 0x0FU)
        != (ADS8688_RANGE_PM_5V12 & 0x0FU)) {
        status = ADS8688_ERR_RANGE_CH1;
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
    if (outputdata == 0) return;

    uint8_t tx[4] = {0x00, 0x00, 0x00, 0x00};  /* NOP cmd + dummy for read */
    uint8_t rx[4];

    for (uint8_t i = 0U; i < chnum; i++) {
        nCS_L;
        HAL_SPI_TransmitReceive(&hspi3, tx, rx, 4, 10);
        nCS_H;
        /* rx[0..1]=dummy(命令回声), rx[2..3]=16bit ADC数据 */
        outputdata[i] = (uint16_t)(((uint16_t)rx[2] << 8) | rx[3]);
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
    uint8_t tx[2] = {(uint8_t)((Addr << 1U) | 0x01U), data};
    nCS_L;
    HAL_SPI_Transmit(&hspi3, tx, 2, 10);
    nCS_H;
}

void Set_CH_Range_Select(uint8_t ch, uint8_t range)
{
    ADS8688A_Write_Program_Register(ch, range);
}

uint8_t ADS8688A_READ_Program_Register(uint8_t Addr)
{
    uint8_t tx[3] = {(uint8_t)(Addr << 1U), 0x00, 0x00};
    uint8_t rx[3];
    nCS_L;
    HAL_SPI_TransmitReceive(&hspi3, tx, rx, 3, 10);
    nCS_H;
    return rx[2];  /* 第3字节是寄存器值 */
}

void ADS8688A_WriteCommandReg(uint16_t command)
{
    uint8_t tx[2] = {(uint8_t)((command >> 8) & 0xFFU), (uint8_t)(command & 0xFFU)};
    nCS_L;
    HAL_SPI_Transmit(&hspi3, tx, 2, 10);
    nCS_H;
}

uint8_t ADS8688A_INIT(void)
{
    uint8_t i;

    Delay(0x1FFFU);

    Enter_RESET_MODE();
    Delay(0xFFFU);  /* 等芯片复位完成 */
    ADS8688A_Write_Program_Register(ADS8688_AUTO_SEQ_REG, 0xFFU);
    Delay(0xFFU);   /* 等寄存器写入生效 */
    i = ADS8688A_READ_Program_Register(ADS8688_AUTO_SEQ_REG);

    return i;
}

/* ===== 硬件 SPI3 (PC10=SCK, PC11=MISO, PC12=MOSI) @10.5MHz ===== */
extern SPI_HandleTypeDef hspi3;

uint8_t ADS8688A_SPI_RB(void)
{
    uint8_t rdata;
    HAL_SPI_Receive(&hspi3, &rdata, 1, 10);
    return rdata;
}

/* 写一字节(不管理CS, 由上层控制) */
void ADS8688A_SPI_WB(uint8_t com)
{
    HAL_SPI_Transmit(&hspi3, &com, 1, 10);
}

void Delay(uint32_t nCount)
{
    volatile uint32_t count = nCount;
    while (count-- != 0U) {
        __NOP();
    }
}
