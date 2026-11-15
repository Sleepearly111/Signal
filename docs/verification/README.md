# Verification Framework

- [FPGA—STM32 高速采样桥 L3 验证](fpga_stm32_adc_bridge_l3.md)
- [DRV-FPGAPKT-001 L2 验证](fpga_packet_protocol_l2.md)

## 验证层级

| Level | 名称 | 示例证据 |
|---|---|---|
| L0 | Draft | 设计文档、首版代码 |
| L1 | Builds | 干净配置与构建 exit 0 |
| L2 | Software Tested | 单元测试、fake bus、板上 selftest |
| L3 | Hardware Tested | 实物器件和信号链测量通过 |
| L4 | Contest Verified | 题目全部指标达标、用户确认收官 |

## 构建记录最低字段

- 仓库、分支、commit、dirty 状态；
- 工具链/CMake/Ninja 版本；
- 配置与完整命令；
- 退出码与警告；
- ELF/MAP 时间；
- Flash、RAM、最大栈风险。

## 硬件记录最低字段

- 板卡和 MCU；
- 固件 commit；
- 器件、供电与接线；
- 信号源设置；
- 测点与仪器；
- 预期、实测、误差；
- 重复次数和稳定时间；
- 结论与待办。

## Golden regression

### test0

应固化：混合信号输入、FFT 识别、A′/B′ 输出、频率字抖动、AD630 搜索到锁定、相位设置和高频边界。

### 2025G

应按功能分开：DDS/VGA 标定、基础(2/3/4)、AD637 高频闭环、发挥(1)四类滤波判断。发挥(2)单独保持 experimental。

验证记录使用 `docs/templates/verification_record_template.md`。

## Framework 验证记录

- [`phase1b_skeleton.md`](phase1b_skeleton.md)：library 空骨架与通用模板的 L1 构建记录。
- [`phase2a_dsp_stats.md`](phase2a_dsp_stats.md)：`DSP-STATS-001` 纯 C 提取、主机单元测试与 Arm 编译记录（L2）。
- [`frequency_measure_core_l2.md`](frequency_measure_core_l2.md)：瞬时频率核心主机软件验证记录。
- [`beat_fll_controller_l2.md`](beat_fll_controller_l2.md)：拍频 FLL 控制核心主机软件验证记录。
- [`framework_audit.md`](framework_audit.md)：Knowledge、Library、索引、链接和脚手架的只读自动审计。
- [`end_to_end_dry_run.md`](end_to_end_dry_run.md)：不依赖现有题目工程的陌生滤波器识别题 L0 端到端架构演练。
- [`end_to_end_regulated_output_dry_run.md`](end_to_end_regulated_output_dry_run.md)：陌生稳幅信号源题从 Knowledge/Library 路由到用户硬件门和代码文件的 L0 演练。
- [`contest_workflow_cores_arm_build.md`](contest_workflow_cores_arm_build.md)：四个新题 L1 核心的 Cortex-M4 严格编译与链接记录；不含测试运行或硬件证据。
- [`scaffolds_arm_build.md`](scaffolds_arm_build.md)：workflow 与 contest_app 两个通用脚手架的 Arm 构建记录。
- [`regulated_signal_output_app_arm_build.md`](regulated_signal_output_app_arm_build.md)：信号输出与 Vpp 反馈闭环参考 App 的 Cortex-M4 构建记录；测试断言未运行。
- [`stm32f4_template_gate.md`](stm32f4_template_gate.md)：STM32F4 新题模板 T0–T9 准入状态。
