#ifndef TRANSFER_CALC_H
#define TRANSFER_CALC_H

#include <stdint.h>

/* 计算 H(s) 在指定频率的幅频增益 |H(jω)| */
float HS_GetGain(uint32_t freq_hz);

/* 计算使电路输出 target_vpp_out 所需的输入 Vpp */
float HS_CalcInputVpp(uint32_t freq_hz, float target_vpp_out);

/* ===== 基本(3)(4) 电路增益 |H(freq)| 查表(来自基本1实测) =====
 * 约束: 基本(3)(4)不能测电路输出,电路增益只能来自基本(1)标定。
 * 存 |H(freq)| = 电路输出Vpp / 电路输入Vpp。
 */
uint8_t HsLut_IsReady(void);              /* |H| 表是否已填(基本1测完置1) */
float   HsGain_Lookup(uint32_t freq_hz);  /* 查 |H(freq)|,点间线性插值 */
float   CircuitGain(uint32_t freq_hz);    /* 电路增益: 表就绪用查表,否则用H(s)公式 */

#endif /* TRANSFER_CALC_H */
