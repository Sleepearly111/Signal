/**
 * @file    filter_comp.c
 * @brief   频率幅度补偿表与查询函数实现
 *
 * 补偿公式: K(f) = 1 + (f/fc)^2, fc = 72340Hz
 * 30k~40kHz, 1kHz步进, 共11点查表 + 线性插值
 *
 * TODO: 当前补偿表为理论计算值。实测标定后替换为真实H(f):
 *   1. PA4/PA5 输出固定码值扫频30k~40kHz
 *   2. 示波器逐点记录测试端口Vpp
 *   3. H(f)=Vpp_out/Vpp_dac, K(f)=1/H(f)
 *   4. 替换下表中attenuation/compensation字段
 *   实测现象: 35kHz/1000mVrms理论示波器2.828Vpp, 实际约2.2Vpp
 */
#include "filter_comp.h"
#include "app_config.h"

#define FILTER_FC_HZ  72340.0f

/* ---- 补偿表: 11点 (freq, H, K) ---- */
typedef struct {
    uint32_t freq;
    float    attenuation;   /* H(f) */
    float    compensation;  /* K(f) */
} FilterCompEntry;

static const FilterCompEntry filter_comp_table[] = {
    {30000, 0.853f, 1.172f},
    {31000, 0.845f, 1.183f},
    {32000, 0.836f, 1.196f},
    {33000, 0.828f, 1.208f},
    {34000, 0.819f, 1.221f},
    {35000, 0.810f, 1.234f},
    {36000, 0.802f, 1.247f},
    {37000, 0.793f, 1.261f},
    {38000, 0.784f, 1.275f},
    {39000, 0.775f, 1.290f},
    {40000, 0.766f, 1.305f},
};
#define FILTER_COMP_COUNT (sizeof(filter_comp_table) / sizeof(filter_comp_table[0]))

/* ---- 查询补偿系数, 线性插值 ---- */
float Filter_GetCompensation(uint32_t freq_hz)
{
    /* 低于下界 */
    if (freq_hz <= filter_comp_table[0].freq) {
        return filter_comp_table[0].compensation;
    }
    /* 高于上界 */
    if (freq_hz >= filter_comp_table[FILTER_COMP_COUNT - 1].freq) {
        return filter_comp_table[FILTER_COMP_COUNT - 1].compensation;
    }
    /* 线性插值 */
    for (uint32_t i = 0; i < FILTER_COMP_COUNT - 1; i++) {
        if (freq_hz >= filter_comp_table[i].freq
            && freq_hz <= filter_comp_table[i + 1].freq) {
            float t = (float)(freq_hz - filter_comp_table[i].freq)
                    / (float)(filter_comp_table[i + 1].freq
                            - filter_comp_table[i].freq);
            return filter_comp_table[i].compensation
                 + t * (filter_comp_table[i + 1].compensation
                      - filter_comp_table[i].compensation);
        }
    }
    /* 兜底: 公式直接算 */
    float ratio = (float)freq_hz / FILTER_FC_HZ;
    return 1.0f + ratio * ratio;
}

/* ---- 查询滤波器衰减 ---- */
float Filter_GetAttenuation(uint32_t freq_hz)
{
    return 1.0f / Filter_GetCompensation(freq_hz);
}

/* ---- 检查补偿后DAC峰值是否超安全边界 ---- */
int Filter_CheckClipping(uint32_t freq_hz, uint16_t vrms_mv)
{
    float comp = Filter_GetCompensation(freq_hz);
    float comp_mv = (float)vrms_mv * comp;

    float dac_vpp = comp_mv * 0.001f * RMS_TO_VPP / THS4001_GAIN;
    float amp = dac_vpp / DAC_FULLSCALE_VPP;
    float amp_fs = (float)(DAC_CENTER - DAC_CODE_MIN_SAFE);
    float peak = (float)DAC_CENTER + amp * amp_fs;

    if (peak > (float)DAC_CODE_MAX_SAFE) {
        return -1;  /* SIGNAL_GEN_CLIPPING */
    }
    return 0;
}
