# ADR 0001: Library L2 模块采用显式可选聚合

- 状态：Accepted
- 日期：2026-07-24
- 范围：`library/` 主机构建入口

## 背景

Signal 已有九个记录为 `experimental / L2 Software Tested` 的模块，但根级 `library/CMakeLists.txt` 只能选择 `DSP-STATS-001`。新题必须自行发现其余子目录，容易漏用模块，也无法通过一个入口检查全部 L2 软件资产。

这些模块仍是 experimental：多数没有 Arm 交叉编译、STM32 集成、资源占用或目标板硬件证据。聚合构建不能改变验证等级。

## 决策

1. 每个模块继续保留自己的独立 CMake 工程；
2. 根 CMake 为九个 L2 模块提供默认关闭的显式选项；
3. `SIGNAL_FRAMEWORK_BUILD_ALL_L2=ON` 用于选择当前全部 L2 模块；
4. `SignalFramework::experimental_bundle` 只聚合本次启用的模块；
5. 稳定入口 `SignalFramework::signal_framework` 不自动链接 experimental 模块；
6. L0 草案和未验证候选不进入 `BUILD_ALL_L2`；
7. 聚合成功最多支持相应软件构建/测试结论，不支持 STM32 或硬件结论。

## 后果

优点：

- AI 和新题工程可以从一个入口发现并选择现有软件模块；
- 默认构建仍为空骨架，不会静默把 experimental 模块带入项目；
- 稳定入口和实验入口保持隔离；
- 后续可以增加统一主机、Arm 和板级验证矩阵。

限制：

- 子模块目前仍各自定义测试目标；
- AD603 模型仍按自身 CMake 方式构建 LUT 依赖；
- 本决策不解决 HAL adapter、CubeMX、板级时序和标定数据；
- 在可用 C 工具链下完成根级全量配置、构建和 CTest 前，聚合入口保持 L0 集成变更状态。

## 验证要求

至少需要：

```text
cmake -S library -B <build> -DSIGNAL_FRAMEWORK_BUILD_ALL_L2=ON
cmake --build <build>
ctest --test-dir <build> --output-on-failure
```

随后还需使用 Arm 工具链逐模块编译，并在目标 STM32 工程记录 Flash、RAM、栈、执行时间和硬件回归。
