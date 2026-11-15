# 电赛信号题能力路由矩阵

> 目的：让 AI 从题目动词和指标稳定路由到方案规则、Knowledge、Library、
> 用户确认门和验证证据，而不是凭目录名或历史题目直接复制。  
> 最后核对：2026-07-24。

## 1. 使用规则

拿到新题后，对每个验收项标记一个或多个能力码：

```text
G 生成    M 测量    C 特性测量
I 识别    L 闭环    A 自动流程
```

然后逐行填写：

1. 题目给出的频率、幅度、阻抗、时间、精度和人工干预条件；
2. 本矩阵要求的 Knowledge；
3. 可选 Library/参考应用及其当前等级；
4. 不可沿用的历史参数；
5. 用户必须确认的 CubeMX/硬件事实；
6. 软件、板级和整题验证门。

一个题目命中多行时取所有约束的并集。任何 `BLOCKED` 项没有解决前，不进入
正式 Adapter/Core 编码。

## 2. 能力覆盖总表

| 能力码 | 典型任务 | Knowledge 路由 | 代码资产 | 当前软件就绪度 | 用户确认门 | 默认回退 |
|---|---|---|---|---|---|---|
| G-DDS | 正弦/方波/三角波、扫频激励 | `K-DEV-001` | AD9833 L2 协议、regulated-output App L1 | 协议和闭环编排可复用；模拟输出未通用验证 | MCLK、SPI/GPIO、输出幅度、负载、幅值测量回路 | 固定频点或 MCU DAC |
| G-DAC | 任意波、双通道、DMA 输出 | `K-STM32-002/003`、`K-ANALOG-001` | `dual_signal_generator` L0、`dac_dma_output` L0、`delay_line` L0、regulated-output App L1 | 输出核心仍是草案；闭环 App 可构建 | TIM TRGO、DAC/DMA、采样更新率、缓冲、负载和幅值测量回路 | 降频、固定波表、单通道 |
| M-AMP-ADC | ADC 测 Vpp/RMS/统计量 | `K-MEAS-001`、`K-STM32-001` | RMS/statistics L2、ADS8688 L2 协议 | 算法/协议可复用 | 有效每通道采样率、Nyquist、量程、端接、同步 | 降频或换检波/RMS |
| M-FPGA | STM32 采样不足时的高速原始波形窗口 | `K-FPGA-001` | `DRV-FPGAPKT-001` L2；`FPGA_STM32高速采样桥` 指定组合 L3 | 协议核可复用；归档位流、接线和 Adapter 仅作同板参考 | FPGA/ADC 型号、引脚复用、采样时钟、Nyquist、窗口长度、SPI 吞吐、DRDY/ACK | 降频、检波/RMS、缩短数据量或改 FPGA 抽取 |
| M-AMP-RMS | AD637/检波器高频测幅 | `K-MEAS-002/004` | RMS L2、LUT L2 | 换算和插值可复用 | 检波带宽、建立时间、峰值因数、DC ADC 标定 | 降低最高频率或人工调试测量 |
| M-FREQ | 输入捕获/ADC 频率测量 | `K-STM32-001` | frequency core L2、FFT L2 | 纯计算可复用 | TIM/DMA 位宽、时钟、窗口、ADC 采样率 | 延长窗口或限制频段 |
| M-PHASE | 双通道相位、时延 | `K-DSP-001/002`、`K-STM32-001` | delay L0、FFT L2 | 数学/FFT可用，链路未通用化 | 共同时间基准、通道同步、回绕、保持 `τ` 或 `fτ` | 只报告时延或限制频点 |
| C-SWEEP | 幅频响应、传递函数 | `K-MEAS-001/002`、`K-ANALOG-001` | sweep L1、transfer response L1、TF2/LUT L2 | 可组合构建，测试未运行 | Vin/Vout 路径、稳定时间、自动切换、标定 | 稀疏点、人工换线（规则允许时） |
| I-FILTER | LP/HP/BP/BS 识别 | 同 C-SWEEP | classifier L1、filter-ID App L1、pipeline build-only | 完整 App 可构建，数据集/阈值未验证 | 频率范围、样件类别、误差和 UNKNOWN 处理 | 只给可确认类别，否则 UNKNOWN |
| I-MOD | AM/CW/2ASK 识别 | `K-DSP-003`、`K-ANALOG-003` | FFT/statistics L2 | AM/CW 有来源经验；2ASK 覆盖不足 | 采样窗口、包络链、候选调制和阈值样本 | 降为 AM/CW 或报告 UNKNOWN |
| L-LEVEL | VGA/输出幅值闭环 | `K-MEAS-003/003A` | AD603 model/LUT L2、workflow L1、regulated-output App L1 | 通用闭环 App 可构建，fake 断言未运行 | 新板标定、执行器范围、最小读数、饱和和 Safe Stop | 固定安全增益 |
| L-CAL | 标定、补偿、记录 | `K-PROC-001`、对应器件 Knowledge | calibration record L1、LUT L2、frequency compensation candidate | 格式可构建，持久化/补偿未完整验证 | board ID、温度、负载、Flash 区域和重标定条件 | 不补偿并标记误差 |
| A-FLOW | 一键、规定时间、回退 | 所有命中项 | contest_app/workflow L1、各 Service | 状态机脚手架已完成 Arm 构建，断言未运行 | 按键/UI、总时间、允许人工步骤、故障恢复 | BASIC 模式、安全停止 |

## 3. 硬否决条件

| 条件 | 结论 |
|---|---|
| `f_signal >= fs_effective/2` 且仍用 ADC 波形算 Vpp/FFT/相位 | 方案否决，改检波、降频或提高真实采样能力 |
| Vin/Vout 物理量、端接或测量时刻不一致 | 不允许计算传递比 |
| 需要相位却只有 RMS/包络 DC | 能力不匹配 |
| 自动题依赖人工换线 | 主方案不满足自动化要求 |
| 闭环无最小读数、饱和、限幅、迭代和总超时 | 不允许进入正式代码 |
| CubeMX 时钟、引脚、DMA、IRQ 未由用户确认 | 只允许纯算法、协议 fake 和接口代码 |
| 只有测试文件或 ARM ELF，断言未运行 | 最高只能按当前构建证据评为 L1 |
| 来源项目通过但提取模块未独立验证 | 不继承来源等级 |

## 4. 代码生成路由

### 4.1 只生成纯软件

适用条件：用户尚未完成 CubeMX。

允许生成：

- 需求配置结构；
- App/Service 状态机；
- 纯 DSP、模型、判型和数据格式；
- Adapter 接口契约；
- fake-port 单元/集成测试；
- CubeMX 操作清单。

禁止生成：

- 猜测的 HAL 句柄和固定引脚；
- 手写时钟树、DMA 映射和中断向量；
- 宣称可烧录的完整工程。

### 4.2 用户配置完成后

用户返回 `.ioc`、生成文件和最小构建日志后：

1. 核对 MCU、时钟、引脚、DMA、IRQ 和 HAL 句柄；
2. 冻结 [`templates/board_adapter_contract_template.md`](templates/board_adapter_contract_template.md)；
3. 只在 `User/` 或 USER CODE 保护段实现 Adapter；
4. 先单器件/单通道，再组合 Service/App；
5. 用户按 [`process/STM32烧录指南.md`](process/STM32烧录指南.md) 烧录并回传证据。

## 5. 当前覆盖结论

### 已具备

- 新题需求、预算、方案 A/B/C 和验证矩阵方法；
- Knowledge 稳定 ID 和来源/限制检索；
- 生成、测量、频率、RMS、FFT、协议、模型等分散能力；
- 扫频、Vin/Vout 响应和滤波器判型的 L1 接口链；
- 非阻塞、超时、故障、Abort 和 Safe Stop 代码形状；
- 用户负责 CubeMX/烧录、AI 等待事实后继续的协作门。

### 尚未证明

- 所有 Library/脚手架在统一主机工具链实际运行；
- filter-ID 组合断言和真实样件阈值；
- 通用生成链/测量链的目标板 Adapter；
- 可复制的新题 STM32F4 模板；
- 任意真实未知题目的 L4 完整闭环。

因此该矩阵是可执行路由入口，不是“Framework 已比赛验证”的声明。
