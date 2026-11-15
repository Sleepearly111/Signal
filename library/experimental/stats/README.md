# DSP-STATS-001 — Statistics / Percentile

> 状态：experimental  
> API 版本：`0.1.0`  
> 验证上限：L2（仅主机软件测试，不代表 STM32 硬件验证）

## 来源

本模块从以下 2023D 原型提取并重新设计接口，来源文件保持不变：

- `2023D/User/Inc/stats.h`
- `2023D/User/Src/stats.c`

来源工作区 SHA-256：

| 文件 | SHA-256 |
|---|---|
| `stats.h` | `474D186345D4FFEA13B9420341264F10C7C058E85B0638DFC59D8FB05A421168` |
| `stats.c` | `6B8F2C03499C1F194F8A1563005A61212A0458AA46CCE132AFFD25B498E1E0E8` |

提取版移除了 `app_config.h`、`FREQ_MAX_SAMPLES`、隐藏的静态数组和 12 位掩码截断行为；模块不引用 HAL、题目全局变量或动态内存。

## API

公开头文件：`experimental/include/signal_stats.h`。

- `signal_stats_percentile_u12()`：使用调用方提供的 4096 项直方图，计算 12 位无符号采样的百分位数。
- `signal_stats_percentile_f32()`：复制到调用方工作区、排序并计算浮点百分位数，不修改输入数组。
- `signal_stats_mid_fraction_u16()`：计算严格落在区间内部 30%–70% 范围的整数样本比例。
- `signal_stats_mid_fraction_f32()`：上述比例的浮点版本。

百分位参数范围为 `[0, 1]`，定义采用最近秩法：`p = 0` 返回最小值，其余位置为 `ceil(p * n) - 1`，`p = 1` 返回最大值。所有函数返回 `signal_stats_status_t`；失败时不改写 `result`。

## 限制

- 空输入返回 `SIGNAL_STATS_STATUS_EMPTY_INPUT`，不产生数值结果。
- U12 输入必须处于 `0..4095`，越界时返回 `SIGNAL_STATS_STATUS_SAMPLE_OUT_OF_RANGE`，不再静默执行 `& 0x0FFF`。
- U12 百分位工作区至少为 `4096 * sizeof(size_t)` 字节；其生命周期和存储位置由调用方决定。
- F32 百分位工作区至少容纳 `count` 个 `float`，排序会改写该工作区。
- 浮点样本、百分位和边界必须为有限数；NaN/Infinity 被拒绝。
- 中段比例沿用来源算法的开区间语义，恰好等于 30% 或 70% 阈值的样本不计入。
- 当前没有 STM32 板级性能、栈/静态 RAM 占用或实时性结论。

## 构建与测试

```shell
cmake -S library -B <build-dir> -DSIGNAL_FRAMEWORK_BUILD_DSP_STATS=ON
cmake --build <build-dir>
ctest --test-dir <build-dir> --output-on-failure
```

主机测试覆盖空输入、单元素、有序数组、固定种子随机数组、65536 元素边界长度，以及工作区不足、U12 越界、非法百分位和非有限浮点输入。2026-07-19 在 Windows/MinGW 主机上 1/1 通过；实际命令和工具链见 `docs/verification/phase2a_dsp_stats.md`。
