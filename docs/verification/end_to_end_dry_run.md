# Framework 端到端演练：合成未知滤波器识别题

> Scenario ID：`DRYRUN-FILTER-ID-001`  
> 日期：2026-07-24  
> 性质：合成题面，用于验证 Framework 工作流，不是真实赛题  
> 当前等级：L0 Architecture Dry Run  
> 禁止结论：本文不证明器件存在、代码可编译、STM32 可运行或滤波器可实测识别

## 1. 合成题面与验收矩阵

### 题面

设计一个基于 STM32F407 的自动测试装置。按键后不得人工换线，在 30 s 内完成：

1. 产生 500 Hz～20 kHz 正弦激励；
2. 测量被测二端口的输入、输出幅值；
3. 在低通、高通、带通、带阻四类中给出类别；
4. 给出一个代表性转折频率或中心频率估计；
5. 显示结果并在失败时给出可观察错误。

本演练额外设定：

- 被测对象输入/输出均为高阻电压接口；
- 不要求相位；
- 主方案可假设有 AD9833、AD603、双 AD637 检波通道和 ADS8688，但开始真实项目时必须复核库存、供电、接口与量程；
- 分类误差和频率估计精度是演练设计目标，不代表任何真实题目指标。

### 验收矩阵

| ID | 输入 | 输出 | 范围/目标 | 自动化 | 验证方法 |
|---|---|---|---|---|---|
| B1 | 频率命令 | 正弦激励 | 500 Hz～20 kHz | 自动 | 示波器仅用于开发标定；正式由参考通道监测 |
| B2 | Vin/Vout | 幅度比 | 每个测试点有质量状态 | 自动 | 双检波通道 + ADC 慢变量 |
| B3 | 未知网络 | LP/HP/BP/BS | 四选一或明确 UNKNOWN | 一键 | 标准样件盲测矩阵 |
| F1 | 幅频点 | 转折/中心频率 | 演练目标 ±10% | 自动 | 未参与拟合的验证点 |
| S1 | 任意故障 | 错误与安全输出 | 30 s 内退出 | 自动 | 故障注入和总超时 |

## 2. 事实、假设与未知项

### 已确认的 Framework 事实

- AD9833、ADS8688 协议模块为 L2 fake-bus，不是硬件驱动完成态；
- AD603 模型、LUT、TF2、statistics 为 L2 主机软件模块；
- AD637/ADS8688/AD603 的硬件经验只覆盖来源项目条件；
- 当前 STM32F4 模板未通过 T0–T9；
- contest_app 与 signal_workflow 为 L0 脚手架。

### 演练假设

- AD9833 MCLK、模拟输出和 AD603 链能覆盖 20 kHz；
- 两个 AD637 通道可以分别观察 Vin/Vout；
- 检波输出在 ADS8688 的直流量程内；
- 参考和测量通道可在一次频点内近似同时稳定；
- 高阻接口不会显著改变被测网络。

### 必须由真实项目确认

- 器件库存、供电、电路和板卡版本；
- AD637 实际建立时间、噪声底和频率相关误差；
- ADS8688 每通道有效慢变量采样率和通道归属；
- AD9833 MCLK、频率字、输出幅度和杂散；
- AD603 安全控制范围及整链标定；
- 30 s 真实最坏时间；
- 四类样件的参数范围和分类精度要求。

## 3. 能力分类

| 要求 | G 生成 | M 测量 | C 特性 | I 识别 | L 闭环 | A 自动 |
|---|---|---|---|---|---|---|
| B1 | ✓ |  |  |  | ✓ | ✓ |
| B2 |  | ✓ | ✓ |  |  | ✓ |
| B3 |  | ✓ | ✓ | ✓ |  | ✓ |
| F1 |  | ✓ | ✓ | ✓ |  | ✓ |
| S1 |  |  |  |  | ✓ | ✓ |

## 4. 信号链与方案 A/B/C

### A 主方案：双 RMS 检波同步慢变量

```text
AD9833 → AD603/缓冲 → Vin 节点 → 被测网络 → Vout 节点
                         ↓                        ↓
                      AD637-A                 AD637-B
                         ↓                        ↓
                     ADS8688 CH0             ADS8688 CH1
                              → STM32 → |H(f)| → 分类/拟合
```

优势：

- ADC 只采 AD637 的慢变 DC，不直接采 20 kHz 载波；
- Vin/Vout 同频点比较，减小源幅频变化影响；
- 不需要人工换线。

风险：

- 双 AD637 通道增益、零点、建立时间和温漂必须分别标定；
- AD637 不提供相位；
- 检波误差会直接进入比值；
- 两通道同步只是同频点近似，不等于同时采样交流波形。

### B 备选：单检波器 + 自动模拟切换

```text
Vin/Vout → 模拟开关 → AD637 → ADS8688
```

优势：减少一个检波器和通道失配。

代价：

- 必须有自动开关；
- 每次切换重新等待稳定；
- 总时间增加；
- 开关导通电阻、串扰和电荷注入需验证。

### C 有条件保底：直接 ADC 波形测量

仅当真实每通道采样率、模拟前端带宽和每周期点数证明足够时使用。20 kHz 至少要求：

```text
fs_effective > 40 kSPS
```

工程上还需要明显高于两点/周期。当前 Knowledge 表明历史 ADS8688 系统有效采样率可能成为瓶颈，因此 C 不能在未实测前作为全频保底。

### 选择

演练选择 A。B 作为器件不足时的工程备选；C 只覆盖经实测证明的低频范围，不能假装满足全题。

## 5. 频率、幅度、时间与误差预算

### 5.1 频率和采样

载波频率最高 20 kHz，但主方案 ADC 输入是 AD637 DC 输出。因此：

- 载波 Nyquist 约束由 AD9833、模拟链和 AD637 带宽处理；
- ADS8688 的 Nyquist 只针对检波后的慢变量；
- 仍需确认慢变量采样率足以判断稳定。

演练初始粗扫点：

```text
0.5, 1, 2, 5, 10, 20 kHz
```

分类不确定时最多增加 10 个自适应点。频点必须由题目范围和实物响应调整，不把该列表写入通用 Library。

### 5.2 幅度

演练目标激励为 1.00 Vpp，但这是项目目标，不是当前硬件事实。

每点必须检查：

- Vin 检波值高于噪声底；
- Vin/Vout 均未超量程；
- AD603、缓冲和被测对象未削顶；
- AD637 在频率、幅度和峰值因数范围内；
- `|H| = Vout / Vin` 的单位和标定一致。

若 Vin 无效，不计算比值，也不自动把 AD603 推到最大。

### 5.3 时间

演练参数（全部为待实测配置，不是经验常数）：

| 阶段 | 暂定预算 |
|---|---:|
| DDS/输出切换 | 20 ms/点 |
| AD637 建立 | 200 ms/点 |
| 32 个慢变量样本 @ 1 kSPS | 32 ms/点 |
| 统计、质量检查和保存 | 20 ms/点 |

粗扫估计：

```text
6 × (20 + 200 + 32 + 20) ms = 1.632 s
```

16 点上限估计为 4.352 s，另给自检、报告、重试和故障恢复预算。只有实测最坏时间后才能证明小于 30 s。

### 5.4 误差

| 来源 | 处理 |
|---|---|
| AD9833/AD603 幅频变化 | 每点使用 Vin 参考通道 |
| 两个 AD637 通道增益差 | 分通道零点/增益/频率标定 |
| ADC 量化与噪声 | 多样本统计、质量状态、噪声底 |
| 稳定不足 | 方差/斜率稳定判据 + 超时 |
| 被测对象负载 | 高阻缓冲并记录端接 |
| 插值/拟合 | 使用独立验证点，不只看训练点 |
| 阈值分类 | 保存置信度，允许 UNKNOWN |

## 6. Knowledge 与 Library 映射

| 资产 | 使用 | 不沿用 | 新题复验 |
|---|---|---|---|
| `K-MEAS-001` | ADS8688 模式、采样率和高频边界 | 历史 GPIO/采样率 | AUTO/MANUAL、样本归属、有效率 |
| `K-MEAS-002` | AD637 转慢变量和稳定等待 | 历史固定等待/换线路径 | 双通道建立时间和误差 |
| `K-MEAS-003` | AD603 标定和闭环保护 | 75.97、-49.30 等历史常数 | 当前板标定、饱和和安全范围 |
| `K-MEAS-004` | RMS 与波形换算边界 | 任意波形 `2√2` | 正弦、带宽和峰值因数 |
| `K-ANALOG-001` | 幅频标定和最终负载条件 | 2024C 补偿表 | 当前链路逐频标定 |
| AD9833 L2 | 频率字与协议 | 固定 MCLK/GPIO | 板级 adapter、时序、模拟输出 |
| ADS8688 L2 | 数字协议 | AUTO 服务和故障恢复 | SPI/CONVST/模式状态机 |
| statistics L2 | 均值/百分位等稳健统计 | 固定阈值 | 噪声数据集 |
| LUT L2 | 标定曲线插值 | 历史表 | 当前原始标定点 |
| TF2 L2 | 候选模型响应 | 自动拟合能力 | 新建拟合/分类器 |
| contest_app L0 | BASIC/ADVANCED/回退/报告 | 通用 mode code | 题目模式与测试 |
| signal_workflow L0 | 单频设置/稳定/测量/修正 | 通用 float | 明确单位和质量结构 |

## 7. 软件架构与文件计划

```text
App/
  filter_id_app.h/.c
    START → SELF_TEST → COARSE_SWEEP → CLASSIFY
          → REFINE → VERIFY → REPORT → SAFE_STOP
          → FALLBACK / FAULT

Services/
  sweep_service.h/.c
  measurement_point_service.h/.c
  amplitude_guard_service.h/.c
  filter_classifier.h/.c
  report_service.h/.c

Adapters/
  board_ad9833.h/.c
  board_ads8688.h/.c
  board_ad603.h/.c
  board_safety.h/.c
  board_hmi.h/.c

Library/
  ad9833, ads8688, statistics, lut_interp, tf2

Core/
  CubeMX generated SPI/GPIO/DAC/TIM/UART
```

### 公开接口草案

```c
filter_id_status_t FilterId_Init(FilterId *app, const FilterId_Config *cfg);
filter_id_status_t FilterId_Start(FilterId *app, uint32_t now_ms);
filter_id_status_t FilterId_Service(FilterId *app, uint32_t now_ms);
bool FilterId_IsDone(const FilterId *app);
filter_id_status_t FilterId_GetResult(
    const FilterId *app,
    FilterId_Result *result
);
filter_id_status_t FilterId_Abort(FilterId *app);
```

结果必须包含：

- 类别或 UNKNOWN；
- 特征频率；
- 分类置信度/质量；
- 已测点数；
- 是否使用回退；
- 错误原因；
- 最后有效频点。

## 8. 状态机

| 状态 | 成功条件 | 超时/错误 | 下一状态 |
|---|---|---|---|
| SAFE_INIT | 输出低增益、总线空闲 | safe-stop 失败 | FAULT |
| SELF_TEST | DDS/ADC/检波通道有合理响应 | 器件/量程错误 | FALLBACK/FAULT |
| AMPLITUDE_SETUP | Vin 有效且未饱和 | 噪声底/削顶 | FALLBACK/FAULT |
| COARSE_SWEEP | 六点均有质量结果 | 单点重试上限 | CLASSIFY/FAULT |
| CLASSIFY | 唯一类别且置信度足够 | 不确定 | REFINE |
| REFINE | 新点降低不确定性 | 点数/时间上限 | VERIFY/UNKNOWN |
| VERIFY | 保留点支持结论 | 验证失败 | UNKNOWN |
| REPORT | HMI/串口报告完成 | 报告超时 | FAULT |
| SAFE_STOP | 输出回安全状态 | 记录错误 | DONE/FAULT |

所有等待通过 `Service(now_ms)` 推进，不在 ISR 或主循环中阻塞。

## 9. 分类策略边界

不使用固定“两点即可四分类”的无条件规则。演练策略：

1. 将每点 `|H|` 归一化到可解释参考；
2. 检查低端、高端和内部点的相对趋势；
3. LP：低端通、高端衰减且主要下降转折唯一；
4. HP：低端衰减、高端通且主要上升转折唯一；
5. BP：两端衰减、内部存在明显峰；
6. BS：两端相对通过、内部存在明显谷；
7. 证据不足、多个峰谷、量程错误或噪声过大时返回 UNKNOWN；
8. 转折/中心频率由相邻点细化，并用未参与拟合点验证。

“通”“阻”“明显”阈值必须从题目指标、标定误差和样件数据集得到，不能由本文固定。

## 10. CubeMX 与 Board Adapter 清单

真实项目必须确认：

| 外设 | 用途 | 待确认 |
|---|---|---|
| SPI/GPIO | AD9833 | MCLK、CS、DATA、CLK、时序 |
| SPI/CONVST/GPIO | ADS8688 | 硬件 SPI、样本流水线、IRQ |
| DAC | AD603 控制 | 参考、码值、电压和安全范围 |
| UART/HMI | 报告 | 非阻塞 TX、协议、超时 |
| TIM | 时间基准/触发 | 时钟树、优先级和回绕 |

回调和所有权另按 `board_adapter_contract_template.md` 填写。

## 11. 编码顺序

1. `filter_classifier` 使用合成响应数据做主机测试；
2. `sweep_service` 使用 fake measurement 测试频点、重试、点数和总超时；
3. 从 signal_workflow 建立单点测量服务；
4. 从 contest_app 建立 BASIC/ADVANCED/回退和报告；
5. 实现 AD9833/ADS8688/AD603 adapter；
6. CubeMX 生成外设层并完成单器件冒烟；
7. 双检波通道标定；
8. 标准 LP/HP/BP/BS 样件训练与盲测；
9. 整题 30 s、故障、Abort 和回退验收。

## 12. 验证矩阵

| ID | 对象 | 方法 | 成功条件 | 等级 |
|---|---|---|---|---|
| V1 | classifier | 合成 LP/HP/BP/BS/UNKNOWN 数据 | 正确分类并拒绝歧义 | L2 目标 |
| V2 | sweep service | fake 点、超时、无效值 | 点序、重试和退出正确 | L2 目标 |
| V3 | App/Workflow | fake adapter | 基础、发挥、回退、Abort | L2 目标 |
| V4 | AD9833 adapter | 逻辑分析仪/示波器 | 频率和波形范围通过 | L3 |
| V5 | 双 AD637 + ADS8688 | 标准正弦/幅度/频率矩阵 | 两通道误差和稳定时间有记录 | L3 |
| V6 | AD603 安全 | 幅度闭环和故障注入 | 不削顶、不越界、超时退出 | L3 |
| V7 | 四类样件 | 训练集和独立盲测集 | 满足真实题目精度 | L3/L4 |
| V8 | 整题 | 按键后一键计时 | 30 s 内结果或明确失败 | L4 目标 |

## 13. 风险与回退

| 风险 | 证据 | 回退 |
|---|---|---|
| 双 AD637 不一致 | 标定残差超限 | 单 AD637 + 自动切换 |
| AD637 太慢 | 真实建立时间超预算 | 减少粗扫点/优化稳定判据，不能静默缩短 |
| 20 kHz 输出或测量失败 | 示波器/参考通道 | 降频仅作调试；正式方案需硬件替换 |
| 分类不确定 | 置信度低/验证点失败 | 增加自适应点或返回 UNKNOWN |
| AD603 饱和 | 削顶/增益不再响应 | 降安全增益并停止该点 |
| ADVANCED 失败 | 超时/器件故障 | 回退 BASIC 诊断或有限能力模式 |

## 14. 演练结论与暴露缺口

### 已证明

- 当前 SOP、方案手册、Knowledge 和 Library 足以形成一份完整、带证据边界的候选架构；
- 能够识别“直接 ADC 测 20 kHz”可能违反有效采样率约束；
- 能够把 App、Service、Adapter 和 Library 分层；
- 能够形成主方案、备选、回退和验证矩阵。

### 未证明

- 没有真实题目勘误、样件范围和评分标准；
- 没有任何 C 构建、STM32、器件或硬件结果；
- 时间和误差预算使用待实测参数；
- 分类规则已有 L1 软件实现和合成测试源码，但尚无测试运行、实测数据集和正式阈值；
- 正式模板仍不可用。

### Framework 暴露的新缺口

1. 通用非阻塞 `sweep_service` 已形成 L1 代码，尚缺主机测试运行和板级 Adapter；
2. 带 UNKNOWN 的 `filter_classifier` 已形成 L1 代码；参数拟合、实测数据集和阈值验证仍缺；
3. `transfer_response` 已形成 Vin/Vout 双通道质量门、增益与截止点 L1 核心；尚缺测试运行和与 sweep Adapter 的板级组合；
4. `calibration_record` 已形成带 schema/board ID/CRC32 的 L1 格式，尚缺 Flash Adapter 和跨版本实测；
5. 缺少真实工具链和脚手架 CTest；
6. 缺少 AD637、AD603、ADS8688 的标准板级 adapter；
7. 缺少通过 T0–T9 的 STM32F4 模板；
8. 缺少真实未知题目的硬件端到端演练。

当前结论保持 `L0 Architecture Dry Run`。该演练不能用于声称 Framework 已达到真实比赛自主完成状态。

软件接口层已新增 build-only 的
`sweep_service → transfer_response → filter_classifier` fake 低通组合测试；
它已生成 ARM ELF，但断言未运行，不能替代真实数据或板级组合验证。
