#include "dds_output.h"
#include "app_config.h"
#include "math.h"
#include "arm_math.h"

/* 复用现有驱动 */
#include "AD9833.h"

/* STM32 HAL DAC */
#include "stm32f4xx_hal.h"
extern DAC_HandleTypeDef hdac;

/* 当前频率 Hz（用于幅度校准时不变更频率） */
static uint32_t current_freq_hz = 1000;

void DDS_Output_Init(void)
{
    /* AD9833 上电初始化 */
    AD9833_Init();

    /* 内部 DAC 初始化，输出 0V → AD603 最低增益 */
    HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 0);
    HAL_DAC_Start(&hdac, DAC_CHANNEL_1);

    /* 默认输出 1kHz */
    DDS_SetFrequency(1000);
}

void DDS_SetFrequency(uint32_t freq_hz)
{
    current_freq_hz = freq_hz;
    /* 分别写入 A/B 两个 AD9833 */
    extern uint8_t wave_flag;
    wave_flag = 0;
    AD9833_SetFrequencyQuick((float)freq_hz, AD9833_OUT_SINUS);
    wave_flag = 1;
    AD9833_SetFrequencyQuick((float)freq_hz, AD9833_OUT_SINUS);
}

/* ===== VGA 控制 (AD603) ===== */

void VGA_SetGain_dB(float gain_dB)
{
    /*
     * AD603 增益曲线: Gain(dB) = AD603_GAIN_SLOPE × Vg + AD603_GAIN_OFFSET
     * → Vg = (gain_dB - AD603_GAIN_OFFSET) / AD603_GAIN_SLOPE
     *
     * TODO: 实测标定 AD603_GAIN_SLOPE, AD603_GAIN_OFFSET
     */
    float vg = (gain_dB - AD603_GAIN_OFFSET) / AD603_GAIN_SLOPE;

    /* 限幅 */
    if (vg < AD603_VG_MIN) vg = AD603_VG_MIN;
    if (vg > AD603_VG_MAX) vg = AD603_VG_MAX;

    /* DAC 12bit 输出: 0~4095 对应 0~VREF */
    uint32_t dac_val = (uint32_t)(vg * 4095.0f / AD603_DAC_VREF);
    if (dac_val > 4095) dac_val = 4095;

    HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_12B_R, dac_val);
}

void VGA_SetGain_Linear(float gain_linear)
{
    if (gain_linear <= 0.0f) return;

    /* 线性倍数 → dB: dB = 20 × log10(linear) */
    float gain_dB = 20.0f * log10f(gain_linear);
    VGA_SetGain_dB(gain_dB);
}

void DDS_Output_Config(uint32_t freq_hz, float gain_linear)
{
    DDS_SetFrequency(freq_hz);
    VGA_SetGain_Linear(gain_linear);
}
