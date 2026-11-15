# Phase 2A — DSP-STATS-001 验证记录

> 日期：2026-07-19  
> 目标等级：L2 / Software Tested  
> 结果：通过（不包含硬件验证）

## 范围与来源

- 模块：`DSP-STATS-001`，statistics / percentile；
- 来源：`2023D/User/Inc/stats.h`、`2023D/User/Src/stats.c`；
- 来源分支/HEAD：`2023d` / `f15b13e8ef2b`；
- 来源仓库基线状态：dirty（4 项），但两个来源文件相对 HEAD 干净；
- 来源工作区 SHA-256：`stats.h` 为 `474D186345D4FFEA13B9420341264F10C7C058E85B0638DFC59D8FB05A421168`，`stats.c` 为 `6B8F2C03499C1F194F8A1563005A61212A0458AA46CCE132AFFD25B498E1E0E8`。

本阶段没有修改 2023D、2024C、stm32f4-template、Core 或 User。实验实现不包含 `app_config.h`、HAL、题目全局变量、隐藏静态可变工作区或动态内存。

## 工具链

| 工具 | 版本 |
|---|---|
| CMake | 4.3.4 |
| Ninja | 1.12.1 |
| Windows 主机 GCC | TDM64 MinGW GCC 4.9.2 |
| Arm 交叉编译 GCC | GNU Arm Embedded 14.2.1 |

## 主机测试

配置、构建和执行：

```text
  -DSIGNAL_FRAMEWORK_BUILD_DSP_STATS=ON
  -DSIGNAL_FRAMEWORK_BUILD_TESTS=ON
```

结果：`DSP-STATS-001` 1/1 通过，0 失败，测试耗时约 0.17 秒。构建启用 `-Wall -Wextra -Wpedantic`，无警告。

另使用 `-std=c11 -Wall -Wextra -Wpedantic -Werror` 直接编译模块和测试，并执行 `signal_stats_tests_strict.exe`；结果为 `DSP-STATS-001: all host tests passed`。

覆盖矩阵：

| 类别 | 覆盖内容 | 结果 |
|---|---|---|
| 空输入 | U12/F32 百分位与中段比例 | 通过 |
| 单元素 | `p=0`、`p=1` | 通过 |
| 有序数组 | 最近秩、开区间 30%–70% | 通过 |
| 随机数组 | 固定种子 257 元素，与独立排序参考比较 | 通过 |
| 边界长度 | U12/F32 各 65536 元素 | 通过 |
| 错误路径 | 工作区不足、U12 越界、非法百分位、Infinity | 通过 |

## Arm 编译检查

以 GNU Arm Embedded 14.2.1 配置 `SIGNAL_FRAMEWORK_BUILD_DSP_STATS=ON`、`SIGNAL_FRAMEWORK_BUILD_TESTS=OFF`，静态库构建成功且无警告。测试源另以 `-std=c11 -Wall -Wextra -Wpedantic -Werror` 编译为 Arm 对象文件成功。

该检查只证明目标工具链可编译，不代表已链接到 STM32 工程或在目标板运行。

## 验证结论

`DSP-STATS-001` 达到 L2：纯 C 实现完成，主机单元测试可执行且通过。当前验证不包含 STM32 工程集成、板级运行时间、RAM 布局、实时性或信号链测量，因此不得标记为 L3/L4。
