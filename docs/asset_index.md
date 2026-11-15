# Signal Asset Index

本文件是 Signal Framework 的长期资产总入口，不替代 `项目资产索引.md` 或 [`library/module_registry.md`](library/module_registry.md)。

> 当前权威范围：用于发现现有资产及其验证边界。模块的具体 API、限制和测试条件仍以模块自己的 `README.md` 与 `docs/verification/` 为准。
>
> 最后核对：2026-07-27（按当前磁盘内容核对；未据此提升任何验证等级）

## 1. Library Assets

| Asset | Category | Source Project | Verification Level | Status |
|---|---|---|---|---|
| Statistics / Percentile (`DSP-STATS-001`) | DSP | 2023D | L2 Software Tested | experimental |
| Capture Interval Frequency (`DSP-INSTFREQ-001`) | DSP | 2023D | L2 Software Tested | experimental |
| Beat-frequency FLL Controller (`DSP-BEATFLL-001`) | DSP/控制 | 2025G | L2 Software Tested | experimental |
| 二阶传递函数（TF2） | DSP | 2025G | L2 Software Tested | experimental |
| 一维 LUT 插值 | DSP | 2025G | L2 Software Tested | experimental |
| AD603 增益模型 | 模拟标定 | 2025G | L2 Software Tested | experimental |
| RMS 计算 | DSP | 2025G | L2 Software Tested | experimental |
| FFT 工具 | DSP | 2025G | L2 Software Tested | experimental |
| AD9833 | 驱动 | 2025G | L2 Software Tested | experimental |
| ADS8688 | 驱动 | 2025G | L2 Software Tested | experimental |
| FPGA Packet (`DRV-FPGAPKT-001`) | 协议 | FPGA—STM32 高速采样桥 | L2 Software Tested | experimental |
| dual_signal_generator | DSP | 2024C | L0 Software Draft | experimental |
| delay_line | DSP | 2024C | L0 Software Draft | experimental |
| dac_dma_output | STM32F4 平台服务 | 2024C | L0 Software Draft | experimental |
| sweep_service | 通用服务 | Framework 陌生题演练 | L1 Builds | experimental |
| filter_classifier | DSP/判型 | Framework 陌生题演练 | L1 Builds | experimental |
| calibration_record | 数据格式 | Framework 陌生题演练 | L1 Builds | experimental |
| transfer_response | DSP/测量处理 | Framework 陌生题演练 | L1 Builds | experimental |
| frequency_compensation | DSP | 2024C | 未独立验证 | candidate |

L2 只代表软件测试，不代表 STM32 硬件验证、通用 `verified` 或比赛验证。

### 1.1 接入可用性

| 类型 | 当前可用方式 | 新题接入前必须补充 |
|---|---|---|
| 纯 DSP L2 模块 | 阅读模块 README 后，以源码或独立 CMake 子目录接入 | Arm 构建、RAM/Flash/耗时、目标板回归 |
| AD9833 / ADS8688 | 使用显式实例与 bus callback 接入 | 板级 adapter、真实时序、模式切换和故障恢复 |
| AD603 模型 | 与一维 LUT 插值依赖一起接入 | 新板卡标定表、负载端闭环验证 |
| L0 模块 | 仅作为 API 和实现草案 | 独立构建、测试、STM32 集成和硬件验证 |

`library/CMakeLists.txt` 已为十一个 L2 模块提供显式选项和 `SignalFramework::experimental_bundle` 聚合目标。该入口用于主机构建和依赖传递；它不证明 Arm 交叉编译、STM32 集成或硬件行为。

### 1.2 Code Scaffolds

| Scaffold | Purpose | Status | Boundary |
|---|---|---|---|
| [`nonblocking_signal_workflow`](../library/experimental/scaffolds/nonblocking_signal_workflow/README.md) | `Init/Start/Service/IsDone/GetResult/Abort`、总超时、有限迭代、错误和安全停机 | L1 Builds | 必须按新题重命名、明确单位、实现 adapter 并重新测试；不计入 19 个 Library 模块 |
| [`contest_app`](../library/experimental/scaffolds/contest_app/README.md) | BASIC/ADVANCED 编排、失败回退、报告、总超时和 Abort | L1 Builds | 必须定义题目模式、报告和 mode result；不实现具体硬件或测量 |

### 1.3 新题通用 L1 核心

| Module | Purpose | Boundary |
|---|---|---|
| [`sweep_service`](../library/experimental/services/sweep_service/README.md) | 非阻塞多频点扫频、每点重试与总超时 | Adapter 提供设频和测量；不拥有 HAL |
| [`filter_classifier`](../library/experimental/DSP/filter_classifier/README.md) | 保守判定 LP/HP/BP/BS/UNKNOWN | 阈值由题目给出；不负责采集和校准 |
| [`calibration_record`](../library/experimental/data/calibration_record/README.md) | 带 schema、板卡 ID 和 CRC32 的校准曲线格式 | 不读写 Flash；CRC 不证明标定有效 |
| [`transfer_response`](../library/experimental/DSP/transfer_response/README.md) | Vin/Vout 质量门、增益/dB 与电平交点 | 不采样；要求两通道物理量一致并先满足测量带宽 |

### 1.4 参考应用与组合证据

| Asset | Status | Purpose | Boundary |
|---|---|---|---|
| [`filter_identification_app`](../library/experimental/reference_apps/filter_identification_app/README.md) | L1 Builds | 完整非阻塞滤波器识别 App，只暴露四个 sweep Adapter 回调 | 不计入 19 个纯 Library 模块；断言和硬件未运行 |
| [`regulated_signal_output_app`](../library/experimental/reference_apps/regulated_signal_output_app/README.md) | L1 Builds | 信号生成、Vpp 测量与有限次幅值反馈闭环，只暴露五个板级 Adapter 回调 | 收敛保留最终输出；故障才 Safe Stop；断言和硬件未运行 |
| [`filter_identification_pipeline`](../library/experimental/integration/filter_identification_pipeline/README.md) | build-only | fake 低通的 sweep→response→classifier 接口组合 | 只证明组合编译链接 |

## 2. Knowledge Assets

完整主题、稳定 ID、任务检索和重复关系见 [`../knowledge/INDEX.md`](../knowledge/INDEX.md)。下表只列主要来源概览。

| Topic | Source | Type | Verification Boundary |
|---|---|---|---|
| ADS8688 模式与高频测量边界 | 2025G | 器件与测量经验 | Knowledge 记录；不自动构成 Library 硬件验证 |
| FPGA + AD9280 高速窗口采集（`K-FPGA-001`） | 2026 FPGA 联调 | 硬件与通信经验 | 指定组合 L3；新题需重做引脚、时序、带宽和标定 |
| AD637 扫频测幅经验 | 2025G | 器件与测量经验 | Knowledge 记录；不自动构成 Library 硬件验证 |
| AD603 标定经验 | 2025G | 模拟标定经验 | Knowledge 记录；不自动构成 Library 硬件验证 |
| 40 kHz 正弦稳定失败分析 | 2025G | 失败案例 | Knowledge 记录；不自动构成 Library 硬件验证 |
| 电赛标定规范 | 2025G | 电赛经验 | Knowledge 记录；不自动构成 Library 硬件验证 |
| RMS 测量经验 | 2025G | 测量经验 | Knowledge 记录；不自动构成 Library 硬件验证 |
| 2023D 高频模拟链调试 | 2023D | 模拟链路经验 | 仅覆盖已记录的 2 MHz 样机条件 |
| 2023D 包络检测与 ADC 标定 | 2023D | 测量与标定经验 | 偏置、RC、阈值和标定表不可跨板照搬 |
| 2023D 双通道同步采集 | 2023D | STM32/DMA 经验 | DMA 映射、窗口和通道配置需重新验证 |
| 2023D AM/CW/2ASK 判决 | 2023D | 算法与判决经验 | AM 有板级证据；2ASK 验证矩阵不完整 |
| AD630 拍频 FLL 与软件 DDS 稳频 | 2025G | 控制与输出经验 | 仅来源整机 1–50 kHz 和题目波形条件；Library 需独立验证 |
| DAC/DMA 输出链路 | 2024C | STM32F4 硬件经验 | 仅覆盖来源时钟、触发、负载和 HAL 条件 |
| RC 滤波与幅频补偿 | 2024C | 模拟链路经验 | 补偿表必须在新板卡与最终负载重新标定 |
| 双通道信号与时延相位 | 2024C | DSP/建模经验 | 区分保持真实时延与保持无量纲相位关系 |
| 高频信号低频等效复现 | 2024C | 电赛方案经验 | 只验证数学关系时可用，不能替代真实射频验收 |

硬件经验属于 Knowledge，不能自动转换为 Library 验证。

Knowledge 当前仍存在中英文分类并存和主题重叠。本索引用于发现资产；遇到同主题多文档时，必须比较来源、验证条件和限制，不得静默拼接结论。

## 3. Project Sources

| Project | Role |
|---|---|
| test0 | 硬件完成基准 |
| 2025G | 首个完整资产来源 |
| 2023D | AI 生成探索项目 |
| 2024C | AI 生成项目 |
| `FPGA_STM32高速采样桥` | EP4CE10 + AD9280 + STM32F407 高速窗口采集整机快照；指定条件 L3 |
| 根级 `stm32f4-template/` | 本队 STM32F407 比赛板基线；Debug/Release 已构建，L1 Builds |
| `projects/AI生成项目/stm32f4-template/` | 历史模板候选；独立 dirty 仓库，当前为 L0 candidate |

当前 `stm32f4-template/` 可作为本队比赛板的新题复制起点；其 FPGA 通信链已经在
上述指定组合完成 L3，但仍不能把该结论扩展为通用模板或新题 L3/L4。新题启动流程见
[`process/新题启动SOP.md`](process/新题启动SOP.md)。
详细准入证据见 [`verification/stm32f4_template_gate.md`](verification/stm32f4_template_gate.md)。

## 4. 新题调用入口

拿到新题后按以下顺序执行：

1. 阅读 `AI_RULES.md`；
2. 执行 `docs/process/新题启动SOP.md`；
3. 用本索引形成候选资产表；
4. 阅读候选 Knowledge 和模块 README；
5. 检查对应验证记录；
6. 建立新题自己的 STM32 适配和验证证据。

不得直接从索引表生成硬件结论或复制旧题参数。
