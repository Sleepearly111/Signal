#include "transfer_calc.h"
#include "app_config.h"
#include "math.h"
#include "arm_math.h"

/*
 * H(s) = B0 / (A2·s² + A1·s + A0)
 * s = jω, ω = 2πf
 *
 * |H(jω)| = B0 / sqrt((A0 - A2·ω²)² + (A1·ω)²)
 */

float HS_GetGain(uint32_t freq_hz)
{
    float omega = 2.0f * (float)M_PI * (float)freq_hz;
    float omega2 = omega * omega;

    /* 分母实部: A0 - A2 × ω² */
    float denom_real = HS_A0 - HS_A2 * omega2;
    /* 分母虚部: A1 × ω */
    float denom_imag = HS_A1 * omega;

    /* |分母| = sqrt(real² + imag²) */
    float denom_mag = sqrtf(denom_real * denom_real + denom_imag * denom_imag);

    if (denom_mag < 1e-9f) return 0.0f;

    return HS_B0 / denom_mag;
}

float HS_CalcInputVpp(uint32_t freq_hz, float target_vpp_out)
{
    float gain = HS_GetGain(freq_hz);

    if (gain < 1e-9f) return OUTPUT_VPP_MAX;

    /* V_in = V_out / gain */
    float vin = target_vpp_out / gain;

    /* 限幅 */
    if (vin > OUTPUT_VPP_MAX) vin = OUTPUT_VPP_MAX;

    return vin;
}

/* ============================================================
 * 基本(3)(4) 电路幅频增益 |H(freq)| 实测查表
 *
 * 约束(题目"说明2"): 基本(3)(4)装置与电路输出端【无反馈连接】,
 *   不能实时测电路输出。所以电路增益 |H| 必须来自【基本(1)】
 *   用示波器的实测标定(允许),不能靠实时闭环。
 *
 * 存的是 |H(freq)| = 电路输出Vpp / 电路输入Vpp。
 * 运行时: 需要的【装置输出】= 目标电路输出 / |H(freq)|,
 *   再对装置自身输出闭环调准(测CH0,允许)。
 *
 * 为何要实测表: 题目要基本(3)(4)电路输出误差≤5%,而基本(1)只保证
 *   理想公式≤10%。理想公式不够准 → 用实测 |H| 表更稳。
 *
 * === 基本(1)怎么填 ===
 *  输入端加 freq、1Vpp 正弦,示波器量电路输出Vpp,则 |H| = 输出/输入。
 *  逐频点填下表的 0.0f。填完把 H_LUT_FILLED 改成 1 → 基本3/4自动用查表。
 *  频点不够密可加行(频率从小到大)。
 * ============================================================ */

#define H_LUT_FILLED  1   /* 基本(1)测完 |H| 表后改成 1 启用查表 */

typedef struct { uint32_t freq_hz; float h; } HPoint;

static const HPoint g_h_table[] = {
    /*  频率Hz  |H|实测(示波器量完填入)   理论参考值 */
    {   100,   4.96f },  /* 实测 */
    {   200,   4.80f },  /* 实测 */
    {   300,   4.40f },  /* 实测 */
    {   400,   4.12f },  /* 实测 */
    {   500,   3.84f },  /* 实测 */
    {   600,   3.52f },  /* 实测 */
    {   700,   3.26f },  /* 实测 */
    {   800,   3.00f },  /* 实测 */
    {   900,   2.80f },  /* 实测 */
    {  1000,   2.54f },  /* 实测 */
    {  1100,   2.36f },  /* 实测 */
    {  1200,   2.18f },  /* 实测 */
    {  1300,   2.04f },  /* 实测 */
    {  1400,   1.90f },  /* 实测 */
    {  1500,   1.78f },  /* 实测 */
    {  1600,   1.66f },  /* 实测 */
    {  1700,   1.56f },  /* 实测 */
    {  1800,   1.44f },  /* 实测 */
    {  1900,   1.38f },  /* 实测 */
    {  2000,   1.30f },  /* 实测 */
    {  2100,   1.22f },  /* 实测 */
    {  2200,   1.16f },  /* 实测 */
    {  2300,   1.08f },  /* 实测 */
    {  2400,   1.02f },  /* 实测 */
    {  2500,   0.98f },  /* 实测 */
    {  2600,   0.92f },  /* 实测 */
    {  2700,   0.88f },  /* 实测 */
    {  2800,   0.84f },  /* 实测 */
    {  2900,   0.80f },  /* 实测 */
    {  3000,   0.76f },  /* 实测 */
};

#define H_TABLE_LEN (sizeof(g_h_table) / sizeof(g_h_table[0]))

uint8_t HsLut_IsReady(void)
{
    return H_LUT_FILLED;
}

float HsGain_Lookup(uint32_t freq_hz)
{
    if (H_TABLE_LEN == 0) return 0.0f;

    /* 超出两端 → 取端点值(不外推) */
    if (freq_hz <= g_h_table[0].freq_hz) {
        return g_h_table[0].h;
    }
    if (freq_hz >= g_h_table[H_TABLE_LEN - 1].freq_hz) {
        return g_h_table[H_TABLE_LEN - 1].h;
    }

    /* 落在哪两点之间 → 线性插值 */
    for (uint32_t i = 1; i < H_TABLE_LEN; i++) {
        if (freq_hz <= g_h_table[i].freq_hz) {
            uint32_t f0 = g_h_table[i - 1].freq_hz;
            uint32_t f1 = g_h_table[i].freq_hz;
            float    h0 = g_h_table[i - 1].h;
            float    h1 = g_h_table[i].h;
            float t = (float)(freq_hz - f0) / (float)(f1 - f0);
            return h0 + t * (h1 - h0);
        }
    }
    return g_h_table[H_TABLE_LEN - 1].h;
}

/* 电路增益 |H(freq)|: 实测表就绪用查表,否则退回 H(s) 理想公式 */
float CircuitGain(uint32_t freq_hz)
{
    if (HsLut_IsReady()) {
        return HsGain_Lookup(freq_hz);
    }
    return HS_GetGain(freq_hz);
}
