# Nonblocking Signal Workflow Scaffold

> 类型：代码脚手架，不是稳定 Library 模块  
> 状态：experimental scaffold  
> 当前验证等级：L1 Builds  
> API：示例契约，复制到新题后必须按题目重命名和裁剪

## 用途

该脚手架把信号题常见的“施加设置 → 等待稳定 → 测量 → 校验 → 计算修正 → 收敛/故障”实现为无 HAL 依赖的非阻塞 C11 状态机。

它适合作为以下服务的起点：

- 幅值闭环；
- 单频测量；
- 扫频中的单点执行器；
- 标定点采集；
- 具有等待、测量、质量检查和有限迭代的自动流程。

它不是通用调度器，也不包含 ADC、DMA、DDS、VGA、滤波器分类或题目 UI。

## 接口

```text
Init
→ Start
→ Service
→ IsDone
→ GetResult
→ Abort
```

板级行为通过 `signal_workflow_ops_t` 注入：

- `apply_request`：设置本次目标；
- `start_measurement`：启动非阻塞测量；
- `poll_measurement`：轮询测量是否完成；
- `compute`：判断收敛并计算有限修正；
- `apply_correction`：施加修正；
- `safe_stop`：成功、故障、超时、Abort 或运行中 Deinit 时进入安全状态。

## 安全语义

- 总超时使用无符号时间差，可跨 `uint32_t` 回绕；
- 无效测量和饱和测量直接进入 FAULT；
- 迭代次数有上限；
- 任何 port I/O 错误进入 FAULT 并调用 `safe_stop`；
- 不使用动态内存、隐藏全局状态、HAL 或固定引脚；
- `safe_stop` 为 best-effort `void` 回调，不得在其中阻塞。

## 复制到新题前必须修改

1. 将 `signal_workflow_*` 改为题目服务名称；
2. 用明确物理单位替换通用 `float target/value/correction`；
3. 把测量质量扩展为题目需要的量程、噪声、置信度和错误码；
4. 明确修正上下限、最小有效读数和饱和策略；
5. 为每个状态增加题目级超时和日志；
6. adapter 只放在项目层，不能把 HAL 塞回本文件；
7. 增加题目正常、边界、故障、Abort 和回绕测试；
8. 在 STM32 上重新记录构建、RAM、栈、执行时间和硬件行为。

## 当前测试意图

`tests/test_signal_workflow.c` 覆盖：

- 一次修正后收敛；
- `uint32_t` 时间回绕下的总超时；
- 无效测量和饱和测量；
- 最大迭代次数；
- port I/O 故障与安全停机；
- Abort；
- 缺失安全停机回调、其他非法配置、重复 Start 和运行中 Deinit。

2026-07-24 使用 STM32Cube bundle Arm GCC/CMake/Ninja 完成根构建 8/8，
静态库和测试 ELF 均生成，因此当前为 L1。测试 ELF 尚未执行，不能根据
测试文件或链接成功标记 L2；证据见
[`../../../../docs/verification/scaffolds_arm_build.md`](../../../../docs/verification/scaffolds_arm_build.md)。
